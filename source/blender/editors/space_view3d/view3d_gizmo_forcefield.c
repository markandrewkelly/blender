/*
 * ***** BEGIN GPL LICENSE BLOCK *****
 *
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
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/editors/space_view3d/view3d_gizmo_forcefield.c
 *  \ingroup spview3d
 */


#include "BLI_blenlib.h"
#include "BLI_math.h"
#include "BLI_utildefines.h"

#include "BKE_context.h"
#include "BKE_object.h"

#include "DNA_object_types.h"
#include "DNA_object_force_types.h"

#include "ED_screen.h"
#include "ED_gizmo_library.h"

#include "UI_resources.h"

#include "MEM_guardedalloc.h"

#include "RNA_access.h"

#include "WM_api.h"
#include "WM_types.h"

#include "view3d_intern.h"  /* own include */

/* -------------------------------------------------------------------- */

/** \name Force Field Gizmos
 * \{ */

static bool WIDGETGROUP_forcefield_poll(const bContext *C, wmGizmoGroupType *UNUSED(wgt))
{
	View3D *v3d = CTX_wm_view3d(C);

	if ((v3d->flag2 & V3D_RENDER_OVERRIDE) ||
	    (v3d->mpr_flag & (V3D_GIZMO_HIDE | V3D_GIZMO_HIDE_CONTEXT)))
	{
		return false;
	}

	Object *ob = CTX_data_active_object(C);

	return (ob && ob->pd && ob->pd->forcefield);
}

static void WIDGETGROUP_forcefield_setup(const bContext *UNUSED(C), wmGizmoGroup *mgroup)
{
	/* only wind effector for now */
	wmGizmoWrapper *wwrapper = MEM_mallocN(sizeof(wmGizmoWrapper), __func__);
	mgroup->customdata = wwrapper;

	wwrapper->gizmo = WM_gizmo_new("GIZMO_WT_arrow_3d", mgroup, NULL);
	wmGizmo *mpr = wwrapper->gizmo;
	RNA_enum_set(mpr->ptr, "transform",  ED_GIZMO_ARROW_XFORM_FLAG_CONSTRAINED);
	ED_gizmo_arrow3d_set_ui_range(mpr, -200.0f, 200.0f);
	ED_gizmo_arrow3d_set_range_fac(mpr, 6.0f);

	UI_GetThemeColor3fv(TH_GIZMO_PRIMARY, mpr->color);
	UI_GetThemeColor3fv(TH_GIZMO_HI, mpr->color_hi);
}

static void WIDGETGROUP_forcefield_refresh(const bContext *C, wmGizmoGroup *mgroup)
{
	wmGizmoWrapper *wwrapper = mgroup->customdata;
	wmGizmo *mpr = wwrapper->gizmo;
	Object *ob = CTX_data_active_object(C);
	PartDeflect *pd = ob->pd;

	if (pd->forcefield == PFIELD_WIND) {
		const float size = (ob->type == OB_EMPTY) ? ob->empty_drawsize : 1.0f;
		const float ofs[3] = {0.0f, -size, 0.0f};
		PointerRNA field_ptr;

		RNA_pointer_create(&ob->id, &RNA_FieldSettings, pd, &field_ptr);
		WM_gizmo_set_matrix_location(mpr, ob->obmat[3]);
		WM_gizmo_set_matrix_rotation_from_z_axis(mpr, ob->obmat[2]);
		WM_gizmo_set_matrix_offset_location(mpr, ofs);
		WM_gizmo_set_flag(mpr, WM_GIZMO_HIDDEN, false);
		WM_gizmo_target_property_def_rna(mpr, "offset", &field_ptr, "strength", -1);
	}
	else {
		WM_gizmo_set_flag(mpr, WM_GIZMO_HIDDEN, true);
	}
}

void VIEW3D_WGT_force_field(wmGizmoGroupType *wgt)
{
	wgt->name = "Force Field Widgets";
	wgt->idname = "VIEW3D_WGT_force_field";

	wgt->flag |= (WM_GIZMOGROUPTYPE_PERSISTENT |
	              WM_GIZMOGROUPTYPE_3D |
	              WM_GIZMOGROUPTYPE_SCALE |
	              WM_GIZMOGROUPTYPE_DEPTH_3D);

	wgt->poll = WIDGETGROUP_forcefield_poll;
	wgt->setup = WIDGETGROUP_forcefield_setup;
	wgt->refresh = WIDGETGROUP_forcefield_refresh;
}

/** \} */
