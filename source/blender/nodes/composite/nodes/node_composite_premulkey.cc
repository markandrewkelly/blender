/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * The Original Code is Copyright (C) 2006 Blender Foundation.
 * All rights reserved.
 */

/** \file
 * \ingroup cmpnodes
 */

#include "node_composite_util.hh"

/* **************** Premul and Key Alpha Convert ******************** */

static bNodeSocketTemplate cmp_node_premulkey_in[] = {
    {SOCK_RGBA, N_("Image"), 1.0f, 1.0f, 1.0f, 1.0f},
    {-1, ""},
};
static bNodeSocketTemplate cmp_node_premulkey_out[] = {
    {SOCK_RGBA, N_("Image")},
    {-1, ""},
};

static int node_composite_gpu_premulkey(GPUMaterial *mat,
                                        bNode *node,
                                        bNodeExecData *UNUSED(execdata),
                                        GPUNodeStack *in,
                                        GPUNodeStack *out)
{
  if (node->custom1 == 1) {
    return GPU_stack_link(mat, node, "color_alpha_unpremultiply", in, out);
  }
  else {
    return GPU_stack_link(mat, node, "color_alpha_premultiply", in, out);
  }
}

void register_node_type_cmp_premulkey(void)
{
  static bNodeType ntype;

  cmp_node_type_base(&ntype, CMP_NODE_PREMULKEY, "Alpha Convert", NODE_CLASS_CONVERTER, 0);
  node_type_socket_templates(&ntype, cmp_node_premulkey_in, cmp_node_premulkey_out);
  node_type_gpu(&ntype, node_composite_gpu_premulkey);

  nodeRegisterType(&ntype);
}
