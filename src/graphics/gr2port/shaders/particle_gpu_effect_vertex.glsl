// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// The GPU particle effect vertex program (0x1b5da3c, in both passes): the model's point on a mesh
// moved by the displacement map, with the colour, normal, tangent, world and view positions and
// the texture coordinates of the material's window. Include particle_gpu_surface_vertex.glsl
// first.

#extension GL_EXT_nonuniform_qualifier : require

layout(set = 0, binding = 0) uniform texture2D textures[];
layout(set = 0, binding = 1) uniform sampler texture_sampler;

// u_nFlags (vertex constant 102): the vertex colour and the displacement.
const uint EffectFlagColor = 0x1u;
const uint EffectFlagDisplace = 0x4000u;

// The colour; the tangent, and its w; the age t in x; the normal, and the world position's x;
// the world position's y and z, and the view position's x and y; the view position's z, and the
// texture coordinates; the material set twice and the set plus 0x8000, as bits. All 0 for
// particles not shown.
layout(location = 0) out vec4 attr0;
layout(location = 1) out vec4 attr1;
layout(location = 2) out vec4 attr2;
layout(location = 3) out vec4 attr3;
layout(location = 4) out vec4 attr4;
layout(location = 5) out vec4 attr5;
layout(location = 6) out vec4 attr6;

// The position moved along the normal (from -1 to 1) by the displacement map's red times
// u_fDispIntensity (vertex constant 101), read at texture coordinates 1 times u_f2DispUVRepeat
// (98 and 99) by 1/1024 plus u_f2DispUVOffset (96 and 97).
vec3 Displaced(EffectMesh mesh, vec3 position, vec3 normal) {
    const ivec2 texcoord1 = MeshTexcoord(mesh, mesh.texcoord1);
    const vec2 place = vec2(
        (1.0 / 1024.0) * (VertexConstant(98u) * float(texcoord1.x)) + VertexConstant(96u),
        (1.0 / 1024.0) * (VertexConstant(99u) * float(texcoord1.y)) + VertexConstant(97u));
    const float height =
        textureLod(sampler2D(textures[nonuniformEXT(mesh.displacement)], texture_sampler), place,
                   0.0)
            .x;
    return (VertexConstant(101u) * height) * normal + position;
}

// The texture coordinates at the mesh's (in 1/1024 units) within the window, v from its far
// edge.
vec2 FlippedCoordinates(vec4 window, ivec2 texcoord) {
    const vec2 place = (1.0 / 1024.0) * vec2(texcoord);
    return vec2(fma(place.x, window.z - window.x, window.x),
                fma(place.y, -(window.w - window.y), window.w));
}

// Writes the outputs and gives the clip position. The colour is the particle's times the vertex
// colour where u_nFlags has it, times the emitter's entry in the colour list.
vec4 ParticleEffectVertex() {
    draw_index = uint(gl_BaseInstance) >> 20;
    EffectMesh mesh = EffectMesh(draws_address + uint64_t(draw_index) * ParticleDrawBytes + 384ul);
    if (mesh.vertices == 0ul) {
        return vec4(0.0);
    }
    vec3 mesh_position = MeshPosition(mesh);
    const ivec2 mesh_texcoord = MeshTexcoord(mesh, mesh.texcoord);
    const vec4 mesh_color = MeshColor(mesh);
    const vec3 mesh_normal = 2.0 * MeshNormal(mesh) + -1.0;
    const vec4 mesh_tangent = MeshTangent(mesh);
    const uint flags = floatBitsToUint(VertexConstant(102u));
    if ((flags & EffectFlagDisplace) != 0u) {
        mesh_position = Displaced(mesh, mesh_position, mesh_normal);
    }
    const uint instance = uint(gl_InstanceIndex) & 0xfffffu;
    const ParticleModelPoint point =
        ParticleModel(DrawWords().words[80] + instance, instance, mesh_position);
    vec4 color = vec4(0.0);
    vec4 tangent = vec4(0.0);
    vec3 normal = vec3(0.0);
    vec3 world = vec3(0.0);
    vec3 view = vec3(0.0);
    vec2 coordinates = vec2(0.0);
    float t = 0.0;
    uvec4 set_bits = uvec4(0u);
    if (point.shown) {
        normal = ParticleNormal(point, mesh_normal);
        tangent = vec4(ParticleNormal(point, mesh_tangent.xyz), mesh_tangent.w);
        world = WorldPosition(point);
        view = MovedView(point);
        coordinates = FlippedCoordinates(SurfaceWindow(point), mesh_texcoord);
        vec4 vertex_color = vec4(1.0);
        if ((flags & EffectFlagColor) != 0u) {
            vertex_color = clamp(max(vec4(0.0), mesh_color), 0.0, 1.0);
        }
        color = ListedColor(point, vertex_color * point.color);
        t = point.t;
        const uint set = point.material - DrawWords().words[87];
        set_bits = uvec4(set, set, set + 0x8000u, 0u);
    }
    attr6 = uintBitsToFloat(set_bits);
    attr5 = vec4(view.z, coordinates, 0.0);
    attr4 = vec4(world.yz, view.xy);
    attr3 = vec4(normal, world.x);
    attr2 = vec4(t, 0.0, 0.0, 0.0);
    attr1 = tangent;
    attr0 = color;
    return ModelClip(point);
}
