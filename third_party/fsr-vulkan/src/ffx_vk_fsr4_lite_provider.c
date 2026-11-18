// SPDX-FileCopyrightText: Copyright 2026 GR2port
/*
 * The FSR 4.1.2 Lite provider entry points, run on the explicit FfxInterface installed before
 * context creation (ffx_vk_fsr4_lite.c), without AMD's DLL.
 *
 * A frame is 10 compute passes: the pre-pass, 8 model passes and the post-pass, in the order,
 * with the bindings and dispatch sizes AMD's provider uses (ffx_vk_fsr4_lite_schedule.c).
 *
 * Jitter is computed with a Halton[2,3] sequence matching what the AMD SDK
 * generates (see ffxQueryDescUpscaleGetJitterOffset handling below).
 */

#include "ffx_vk_fsr4_lite_types.h"
#include "ffx_vk_fsr4_lite.h"
#include "ffx_vk_fsr4_lite_schedule.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* The interface is copied during context creation; it is not a renderer
 * global and does not need to remain installed for dispatch/destroy. */
static FfxInterface *g_fsr4_backend_interface;

void
ffxFsr4V07SetBackendInterface(FfxInterface *backend)
{
    g_fsr4_backend_interface = backend;
}

/* ── internal context ────────────────────────────────────────────────────── */

typedef struct Fsr4Context {
    FfxInterface        iface;          /* copy of backend interface        */
    FfxUInt32           effectId;       /* effectContextId from backend     */
    FfxApiDimensions2D  maxRenderSize;
    FfxApiDimensions2D  maxUpscaleSize;
    uint32_t            flags;
    bool                valid;

    /* Internal resources (created once, persist across frames) */
    FfxResourceInternal ri_scratch;       /* Network activations              */
    FfxResourceInternal ri_initializer;   /* The network's initializer        */
    FfxResourceInternal ri_history;       /* Previous upscaled output         */
    FfxResourceInternal ri_recurrent;     /* Recurrent state: pre reads, post writes */
    FfxResourceInternal ri_reprojected;   /* Pre-pass to post-pass bridge     */
    FfxResourceInternal ri_exposure;      /* Exposure pair the post-pass writes (2x1) */
    FfxResourceInternal ri_input_exposure; /* Explicit exposure (1, 1), 1x1   */
    float               previous_pre_exposure;

    bool                resources_ok;     /* true if internal resources exist */
} Fsr4Context;

/* ── Halton sequence helper ──────────────────────────────────────────────── */

static float halton(int base, int index)
{
    float f = 1.0f, r = 0.0f;
    int   i = index;
    while (i > 0) {
        f /= (float)base;
        r += f * (float)(i % base);
        i /= base;
    }
    return r;
}

static int jitter_phase_count(uint32_t display_w, uint32_t render_w)
{
    /* FSR4 formula: ceil(8 * (display_w / render_w)^2), clamped to sensible range */
    if (render_w == 0) return 8;
    float ratio = (float)display_w / (float)render_w;
    int   count = (int)ceilf(8.0f * ratio * ratio);
    if (count < 1)   count = 1;
    if (count > 256) count = 256;
    return count;
}

/* ── Internal resource creation helper ───────────────────────────────────── */

static FfxErrorCode create_internal_tex(Fsr4Context *c, uint32_t w, uint32_t h,
    FfxSurfaceFormat fmt, FfxResourceInternal *out)
{
    FfxCreateResourceDescription desc;
    memset(&desc, 0, sizeof(desc));
    desc.type    = FFX_RESOURCE_TYPE_TEXTURE2D;
    desc.format  = fmt;
    desc.width   = w;
    desc.height  = h;
    return c->iface.fpCreateResource(&c->iface, &desc, c->effectId, out);
}

static FfxErrorCode create_internal_buf(Fsr4Context *c, uint32_t size_bytes,
    const void *initial_data,
    FfxResourceInternal *out)
{
    FfxCreateResourceDescription desc;
    memset(&desc, 0, sizeof(desc));
    desc.type         = FFX_RESOURCE_TYPE_BUFFER;
    desc.format       = FFX_SURFACE_FORMAT_UNKNOWN;
    desc.width        = size_bytes;  /* buffer "width" = size in bytes */
    desc.height       = 0;
    desc.initDataSize = size_bytes;
    desc.initData     = initial_data;
    return c->iface.fpCreateResource(&c->iface, &desc, c->effectId, out);
}

static FfxErrorCode create_internal_resources(Fsr4Context *c)
{
    FfxErrorCode err;
    uint32_t dw = c->maxUpscaleSize.width;
    uint32_t dh = c->maxUpscaleSize.height;
    size_t initializer_size = 0;
    const void *initializer =
        ffxFsr4VkGetModelInitializer(&c->iface, &initializer_size);

    /* These are generated-code ABI sizes, not resolution estimates. */
    size_t scratch_size = ffxFsr4GetDot4ScratchSize(dw, dh);
    if (!scratch_size || scratch_size > UINT32_MAX ||
        !initializer || !initializer_size ||
        initializer_size > UINT32_MAX)
        return FFX_ERROR_INVALID_ARGUMENT;

    err = create_internal_buf(c, (uint32_t)scratch_size, NULL, &c->ri_scratch);
    if (err != FFX_OK) return err;
    err = create_internal_buf(c, (uint32_t)initializer_size, initializer,
                              &c->ri_initializer);
    if (err != FFX_OK) return err;

    /* History: the previous upscaled output. */
    err = create_internal_tex(c, dw, dh, FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT,
                              &c->ri_history);
    if (err != FFX_OK) return err;

    /* Recurrent state, RGBA8 UNORM like AMD's provider; the post-pass SPIR-V is patched to
       declare it Rgba8. */
    err = create_internal_tex(c, dw, dh, FFX_SURFACE_FORMAT_R8G8B8A8_UNORM,
                              &c->ri_recurrent);
    if (err != FFX_OK) return err;

    err = create_internal_tex(c, dw, dh, FFX_SURFACE_FORMAT_R16G16B16A16_FLOAT,
                              &c->ri_reprojected);
    if (err != FFX_OK) return err;
    err = create_internal_tex(c, 2, 1, FFX_SURFACE_FORMAT_R32_FLOAT, &c->ri_exposure);
    if (err != FFX_OK) return err;
    err = create_internal_tex(c, 1, 1, FFX_SURFACE_FORMAT_R32G32_FLOAT,
                              &c->ri_input_exposure);
    if (err != FFX_OK) return err;

    /* Persistent images start from known contents once per context; the explicit exposure
       holds (1, 1) as AMD's provider uploads it. */
    {
        const FfxResourceInternal clear_targets[] = {
            c->ri_history, c->ri_recurrent, c->ri_reprojected, c->ri_exposure,
            c->ri_input_exposure
        };
        for (size_t i = 0; i < sizeof(clear_targets) / sizeof(clear_targets[0]); ++i) {
            FfxGpuJobDescription clear_job;
            memset(&clear_job, 0, sizeof(clear_job));
            clear_job.jobType = FFX_GPU_JOB_CLEAR_FLOAT;
            clear_job.clearJobDescriptor.target = clear_targets[i];
            if (i == 4) {
                clear_job.clearJobDescriptor.color[0] = 1.0f;
                clear_job.clearJobDescriptor.color[1] = 1.0f;
            }
            err = c->iface.fpScheduleGpuJob(&c->iface, &clear_job);
            if (err != FFX_OK) return err;
        }
    }

    c->previous_pre_exposure = 0.0f;
    c->resources_ok  = true;
    return FFX_OK;
}

/* ── ffxCreateContext ─────────────────────────────────────────────────────── */

ffxReturnCode_t ffxFsr4V07CreateContext(ffxContext *ctx,
    ffxCreateContextDescHeader *desc,
    const ffxAllocationCallbacks *mem)
{
    (void)mem;

    if (!ctx || !desc) return FFX_API_RETURN_ERROR_PARAMETER;
    if (!g_fsr4_backend_interface) return FFX_API_RETURN_ERROR;

    /* Find the upscale descriptor in the linked list */
    ffxCreateContextDescUpscale *upscaleDesc = NULL;
    for (ffxCreateContextDescHeader *h = desc; h; h = h->pNext) {
        if (h->type == FFX_API_CREATE_CONTEXT_DESC_TYPE_UPSCALE) {
            upscaleDesc = (ffxCreateContextDescUpscale *)h;
            break;
        }
    }
    if (!upscaleDesc) return FFX_API_RETURN_ERROR_PARAMETER;
    if (!upscaleDesc->maxRenderSize.width ||
        !upscaleDesc->maxRenderSize.height ||
        !upscaleDesc->maxUpscaleSize.width ||
        !upscaleDesc->maxUpscaleSize.height ||
        upscaleDesc->maxUpscaleSize.width > FFX_FSR4_DOT4_MAX_OUTPUT_WIDTH ||
        upscaleDesc->maxUpscaleSize.height > FFX_FSR4_DOT4_MAX_OUTPUT_HEIGHT)
        return FFX_API_RETURN_ERROR_PARAMETER;

    Fsr4Context *c = (Fsr4Context *)calloc(1, sizeof(Fsr4Context));
    if (!c) return FFX_API_RETURN_ERROR;

    /* Copy the interface so the caller may clear its creation binding. */
    c->iface          = *g_fsr4_backend_interface;
    c->maxRenderSize  = upscaleDesc->maxRenderSize;
    c->maxUpscaleSize = upscaleDesc->maxUpscaleSize;
    c->flags          = upscaleDesc->flags;
    c->valid          = true;

    /* Initialise the Vulkan backend context (creates pipelines) */
    FfxEffectBindlessConfig bindlessCfg = {0, 0, 0, 1};
    FfxErrorCode err = c->iface.fpCreateBackendContext(
        &c->iface, FFX_EFFECT_FSR4UPSCALER, &bindlessCfg, &c->effectId);
    if (err != FFX_OK) {
        free(c);
        return FFX_API_RETURN_ERROR;
    }

    /* Create internal GPU resources (scratch, history, recurrent, etc.) */
    err = create_internal_resources(c);
    if (err != FFX_OK) {
        c->iface.fpDestroyBackendContext(&c->iface, c->effectId);
        free(c);
        return FFX_API_RETURN_ERROR;
    }

    *ctx = (ffxContext)c;
    return FFX_API_RETURN_OK;
}

/* ── ffxDestroyContext ────────────────────────────────────────────────────── */

ffxReturnCode_t ffxFsr4V07DestroyContext(ffxContext *ctx,
    const ffxAllocationCallbacks *mem)
{
    (void)mem;
    if (!ctx || !*ctx) return FFX_API_RETURN_ERROR_PARAMETER;

    Fsr4Context *c = (Fsr4Context *)(*ctx);
    if (c->valid)
        c->iface.fpDestroyBackendContext(&c->iface, c->effectId);

    free(c);
    *ctx = NULL;
    return FFX_API_RETURN_OK;
}

/* ── ffxQuery ─────────────────────────────────────────────────────────────── */

ffxReturnCode_t ffxFsr4V07Query(ffxContext *ctx, ffxQueryDescHeader *desc)
{
    if (!desc) return FFX_API_RETURN_ERROR_PARAMETER;

    Fsr4Context *c = (ctx && *ctx) ? (Fsr4Context *)(*ctx) : NULL;

    for (ffxQueryDescHeader *h = desc; h; h = h->pNext) {

        if (h->type == FFX_API_QUERY_DESC_TYPE_UPSCALE_GET_JITTER_PHASE_COUNT) {
            ffxQueryDescUpscaleGetJitterPhaseCount *q =
                (ffxQueryDescUpscaleGetJitterPhaseCount *)h;
            if (q->pOutPhaseCount)
                *q->pOutPhaseCount = jitter_phase_count(
                    q->displayWidth, q->renderWidth);
            continue;
        }

        if (h->type == FFX_API_QUERY_DESC_TYPE_UPSCALE_GET_JITTER_OFFSET) {
            ffxQueryDescUpscaleGetJitterOffset *q =
                (ffxQueryDescUpscaleGetJitterOffset *)h;
            if (q->pOutX && q->pOutY) {
                /* Halton[2,3], 1-indexed, shifted to [-0.5, 0.5] */
                int idx = (q->index % q->phaseCount) + 1;
                *q->pOutX = halton(2, idx) - 0.5f;
                *q->pOutY = halton(3, idx) - 0.5f;
            }
            continue;
        }
    }

    (void)c;
    return FFX_API_RETURN_OK;
}

/* ── ffxDispatch ─────────────────────────────────────────────────────────── */

/*
 * Dispatch a single upscale frame: the 10 passes in order. srvTextures[i] binds t-register i,
 * uavTextures[i] u-register i, srvBuffers[0] the initializer (t21), uavBuffers[0] the scratch
 * buffer (u13), cbs[0] CsTensorSizes (b0) and cbs[1] the per-frame constants (b1). Unused slots
 * hold RI_NONE; bindings a pass does not declare are skipped.
 */

/* Register one FfxApiResource.  A missing resource and a backend registration
 * failure are different conditions: mandatory dispatch inputs must propagate
 * either instead of becoming UINT32_MAX descriptors that silently skip. */
static FfxErrorCode reg_resource(Fsr4Context *c,
    const FfxApiResource *res, FfxUInt32 effectId, FfxResourceInternal *out)
{
    if (!c || !res || !res->resource || !out)
        return FFX_ERROR_INVALID_POINTER;
    out->internalIndex = UINT32_MAX;
    return c->iface.fpRegisterResource(&c->iface, res, effectId, out);
}

/* Sentinel for "no resource at this slot" — writeFullDs/writeModelDs skip these */
static const FfxResourceInternal RI_NONE = { UINT32_MAX };

ffxReturnCode_t ffxFsr4V07Dispatch(ffxContext *ctx, const ffxDispatchDescHeader *desc)
{
    if (!ctx || !*ctx || !desc) return FFX_API_RETURN_ERROR_PARAMETER;
    Fsr4Context *c = (Fsr4Context *)(*ctx);
    if (!c->valid || !c->resources_ok) return FFX_API_RETURN_ERROR;

    /* Find the upscale dispatch descriptor */
    const ffxDispatchDescUpscale *d = NULL;
    for (const ffxDispatchDescHeader *h = desc; h; h = h->pNext) {
        if (h->type == FFX_API_DISPATCH_DESC_TYPE_UPSCALE) {
            d = (const ffxDispatchDescUpscale *)h;
            break;
        }
    }
    if (!d) return FFX_API_RETURN_ERROR_PARAMETER;

    VkCommandBuffer cmd = (VkCommandBuffer)d->commandList;
    if (!cmd) return FFX_API_RETURN_ERROR_PARAMETER;
    uint32_t rw = d->renderSize.width;
    uint32_t rh = d->renderSize.height;
    uint32_t dw = d->upscaleSize.width  ? d->upscaleSize.width  : c->maxUpscaleSize.width;
    uint32_t dh = d->upscaleSize.height ? d->upscaleSize.height : c->maxUpscaleSize.height;
    FfxFsr4Dot4Schedule schedule;
    if (!rw || !rh || rw > c->maxRenderSize.width ||
        rh > c->maxRenderSize.height || dw > c->maxUpscaleSize.width ||
        dh > c->maxUpscaleSize.height ||
        !ffxFsr4BuildDot4Schedule(dw, dh, &schedule))
        return FFX_API_RETURN_ERROR_PARAMETER;

    /* Register external resources for this frame */
    FfxResourceInternal ri_color = RI_NONE;
    FfxResourceInternal ri_depth = RI_NONE;
    FfxResourceInternal ri_mv = RI_NONE;
    FfxResourceInternal ri_output = RI_NONE;
    FfxErrorCode register_error = reg_resource(
        c, &d->color, c->effectId, &ri_color);
    if (register_error == FFX_OK)
        register_error = reg_resource(c, &d->depth, c->effectId, &ri_depth);
    if (register_error == FFX_OK)
        register_error = reg_resource(
            c, &d->motionVectors, c->effectId, &ri_mv);
    if (register_error == FFX_OK)
        register_error = reg_resource(c, &d->output, c->effectId, &ri_output);
    if (register_error != FFX_OK) {
        /* Registration is append-only for the frame.  Always discard already
         * registered externals before surfacing the error. */
        c->iface.fpUnregisterResources(
            &c->iface, (FfxCommandList)cmd, c->effectId);
        return FFX_API_RETURN_ERROR;
    }

    /* Shorthand for internal resources */
    FfxResourceInternal ri_history     = c->ri_history;
    FfxResourceInternal ri_recurrent   = c->ri_recurrent;
    FfxResourceInternal ri_reprojected = c->ri_reprojected;
    FfxResourceInternal ri_exposure    = c->ri_exposure;
    FfxResourceInternal ri_input_exposure = c->ri_input_exposure;
    FfxResourceInternal ri_scratch     = c->ri_scratch;
    FfxResourceInternal ri_initializer = c->ri_initializer;

    /* The per-frame MLSR_Optimized_Constants (104 bytes). */
    struct {
        float    inv_size[2];
        float    scale[2];
        float    inv_scale[2];
        float    jitter[2];
        float    mv_scale[2];
        float    tex_size[2];
        float    max_renderSize[2];
        float    fMotionVectorJitterCancellation[2];
        uint32_t width;
        uint32_t height;
        uint32_t reset;
        uint32_t width_lr;
        uint32_t height_lr;
        float    preExposure;
        float    previous_preExposure;
        uint32_t rcas_enabled;
        float    rcas_sharpness;
        float    _pad1;
    } cb;
    memset(&cb, 0, sizeof(cb));
    cb.inv_size[0]    = 1.0f / (float)dw;
    cb.inv_size[1]    = 1.0f / (float)dh;
    cb.scale[0]       = (float)dw / (float)rw;
    cb.scale[1]       = (float)dh / (float)rh;
    cb.inv_scale[0]   = (float)rw / (float)dw;
    cb.inv_scale[1]   = (float)rh / (float)dh;
    cb.jitter[0]      = d->jitterOffset.x;
    cb.jitter[1]      = d->jitterOffset.y;
    cb.mv_scale[0]    = d->motionVectorScale.x / (float)rw;
    cb.mv_scale[1]    = d->motionVectorScale.y / (float)rh;
    cb.tex_size[0]    = (float)c->maxUpscaleSize.width;
    cb.tex_size[1]    = (float)c->maxUpscaleSize.height;
    cb.max_renderSize[0] = (float)c->maxRenderSize.width;
    cb.max_renderSize[1] = (float)c->maxRenderSize.height;
    cb.width          = dw;
    cb.height         = dh;
    cb.reset          = d->reset ? 1u : 0u;
    cb.width_lr       = rw;
    cb.height_lr      = rh;
    cb.preExposure    = (d->preExposure > 0.0f) ? d->preExposure : 1.0f;
    cb.previous_preExposure = c->previous_pre_exposure;

    FfxConstantBuffer cbHandle = {{0}};
    FfxConstantBuffer tensorHandle = {{0}};
    if (c->iface.fpStageConstantBufferDataFunc(&c->iface, &cb,
            (FfxUInt32)sizeof(cb), &cbHandle) != FFX_OK ||
        c->iface.fpStageConstantBufferDataFunc(&c->iface, schedule.tensorSizes,
            (FfxUInt32)sizeof(schedule.tensorSizes), &tensorHandle) != FFX_OK)
        goto dispatch_error;

    for (uint32_t pass = 0; pass < FFX_FSR4_PASS_COUNT; pass++) {
        FfxGpuJobDescription job;
        memset(&job, 0, sizeof(job));
        job.jobType = FFX_GPU_JOB_COMPUTE;
        FfxComputeJobDescription *cj = &job.computeJobDescriptor;
        for (uint32_t i = 0; i < FFX_MAX_NUM_SRVS; ++i)
            cj->srvTextures[i].resource = RI_NONE;
        for (uint32_t i = 0; i < FFX_MAX_NUM_UAVS; ++i)
            cj->uavTextures[i].resource = RI_NONE;

        /* The backend encodes pass + 1 so the pre-pass is not a null pipeline handle. */
        cj->pipeline.pipeline = (FfxPipeline)(uintptr_t)(pass + 1u);
        cj->pipeline.srvBufferCount = 1;
        cj->srvBuffers[0].resource = ri_initializer;
        cj->pipeline.uavBufferCount = 1;
        cj->uavBuffers[0].resource = ri_scratch;
        cj->pipeline.constCount = 2;
        cj->cbs[0] = tensorHandle;
        cj->cbs[1] = cbHandle;

        if (pass == 0) {
            cj->pipeline.srvTextureCount = 18;
            cj->srvTextures[0].resource = ri_history;
            cj->srvTextures[1].resource = ri_mv;
            cj->srvTextures[2].resource = ri_depth;
            cj->srvTextures[3].resource = ri_color;
            cj->srvTextures[4].resource = ri_recurrent;
            cj->srvTextures[6].resource = ri_input_exposure;
            cj->srvTextures[17].resource = ri_exposure;
            cj->pipeline.uavTextureCount = 4;
            cj->uavTextures[3].resource = ri_reprojected;
            cj->dimensions[0] = schedule.pre.x;
            cj->dimensions[1] = schedule.pre.y;
            cj->dimensions[2] = schedule.pre.z;
        } else if (pass == FFX_FSR4_PASS_COUNT - 1u) {
            cj->pipeline.srvTextureCount = 10;
            cj->srvTextures[3].resource = ri_color;
            cj->srvTextures[6].resource = ri_input_exposure;
            cj->srvTextures[9].resource = ri_reprojected;
            cj->pipeline.uavTextureCount = 10;
            cj->uavTextures[1].resource = ri_history;
            cj->uavTextures[2].resource = ri_output;
            cj->uavTextures[6].resource = ri_recurrent;
            cj->uavTextures[9].resource = ri_exposure;
            cj->dimensions[0] = schedule.post.x;
            cj->dimensions[1] = schedule.post.y;
            cj->dimensions[2] = schedule.post.z;
        } else {
            cj->dimensions[0] = schedule.model[pass - 1u].x;
            cj->dimensions[1] = schedule.model[pass - 1u].y;
            cj->dimensions[2] = schedule.model[pass - 1u].z;
        }

        if (c->iface.fpScheduleGpuJob(&c->iface, &job) != FFX_OK)
            goto dispatch_error;
    }

    /* ── Execute all jobs ──────────────────────────────────────────────────── */
    if (c->iface.fpExecuteGpuJobs(&c->iface, (FfxCommandList)cmd,
                                  c->effectId) != FFX_OK)
        goto dispatch_error;

    /* ── Unregister per-frame external resources ──────────────────────────── */
    if (c->iface.fpUnregisterResources(
            &c->iface, (FfxCommandList)cmd, c->effectId) != FFX_OK)
        return FFX_API_RETURN_ERROR;

    c->previous_pre_exposure = cb.preExposure;

    return FFX_API_RETURN_OK;

dispatch_error:
    c->iface.fpUnregisterResources(
        &c->iface, (FfxCommandList)cmd, c->effectId);
    return FFX_API_RETURN_ERROR;
}

int
ffxFsr4GetDebugResource(ffxContext *ctx, FfxFsr4DebugResource resource,
    FfxApiResource *out_resource)
{
    Fsr4Context *c;
    FfxResourceInternal internal;

    if (!ctx || !*ctx || !out_resource)
        return 0;
    c = (Fsr4Context *)(*ctx);
    if (!c->valid || !c->resources_ok || !c->iface.fpGetResource ||
        !c->iface.fpGetResourceDescription)
        return 0;
    switch (resource) {
    case FFX_FSR4_DEBUG_RESOURCE_HISTORY:
        internal = c->ri_history;
        break;
    case FFX_FSR4_DEBUG_RESOURCE_REPROJECTED:
        internal = c->ri_reprojected;
        break;
    default:
        return 0;
    }
    *out_resource = c->iface.fpGetResource(&c->iface, internal);
    out_resource->description = c->iface.fpGetResourceDescription(
        &c->iface, internal);
    return out_resource->resource && out_resource->description.width &&
        out_resource->description.height;
}

/* ── ffxConfigure (stub — not needed for basic upscaling) ─────────────────── */

ffxReturnCode_t ffxFsr4V07Configure(ffxContext *ctx, const ffxApiHeader *desc)
{
    (void)ctx; (void)desc;
    return FFX_API_RETURN_OK;
}
