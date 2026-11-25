// SPDX-FileCopyrightText: Copyright 2026 GR2port
// SPDX-License-Identifier: GPL-2.0-or-later

// A pixel program's per-draw loop relies on the lanes of different turns of the loop staying apart.
// A build with MAXIMAL_RECONVERGENCE asks the driver for that with its entry point's attribute; the
// build without it is for drivers that lack VK_KHR_shader_maximal_reconvergence.
#ifdef MAXIMAL_RECONVERGENCE
#extension GL_EXT_maximal_reconvergence : require
#define MAXIMALLY_RECONVERGES [[maximally_reconverges]]
#else
#define MAXIMALLY_RECONVERGES
#endif
