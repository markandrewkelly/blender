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
 * Copyright 2020, Blender Foundation.
 */

/** \file
 * \ingroup draw_engine
 *
 * Draw engine to draw the Image/UV editor
 */

#include "DRW_render.h"

#include <memory>
#include <optional>

#include "BKE_image.h"
#include "BKE_main.h"
#include "BKE_object.h"

#include "DNA_camera_types.h"
#include "DNA_screen_types.h"

#include "IMB_imbuf.h"
#include "IMB_imbuf_types.h"

#include "ED_image.h"

#include "GPU_batch.h"

#include "image_drawing_mode.hh"
#include "image_engine.h"
#include "image_private.hh"
#include "image_space_image.hh"
#include "image_space_node.hh"

namespace blender::draw::image_engine {

static std::unique_ptr<AbstractSpaceAccessor> space_accessor_from_context(
    const DRWContextState *draw_ctx)
{
  const char space_type = draw_ctx->space_data->spacetype;
  if (space_type == SPACE_IMAGE) {
    return std::make_unique<SpaceImageAccessor>((SpaceImage *)draw_ctx->space_data);
  }
  if (space_type == SPACE_NODE) {
    return std::make_unique<SpaceNodeAccessor>((SpaceNode *)draw_ctx->space_data);
  }
  BLI_assert_unreachable();
  return nullptr;
}

template<
    /** \brief Drawing mode to use.
     *
     * Useful during development to switch between drawing implementations.
     */
    typename DrawingMode = ScreenSpaceDrawingMode<OneTextureMethod>>
class ImageEngine {
 private:
  const DRWContextState *draw_ctx;
  IMAGE_Data *vedata;
  std::unique_ptr<AbstractSpaceAccessor> space;
  DrawingMode drawing_mode;

 public:
  ImageEngine(const DRWContextState *draw_ctx, IMAGE_Data *vedata)
      : draw_ctx(draw_ctx), vedata(vedata), space(space_accessor_from_context(draw_ctx))
  {
  }

  virtual ~ImageEngine() = default;

  void cache_init()
  {
    IMAGE_InstanceData *instance_data = vedata->instance_data;
    drawing_mode.cache_init(vedata);
    const ARegion *region = draw_ctx->region;
    instance_data->view = space->create_view_override(region);
  }

  void cache_populate()
  {
    IMAGE_InstanceData *instance_data = vedata->instance_data;
    Main *bmain = CTX_data_main(draw_ctx->evil_C);
    instance_data->image = space->get_image(bmain);
    if (instance_data->image == nullptr) {
      /* Early exit, nothing to draw. */
      return;
    }
    instance_data->flags.do_tile_drawing = instance_data->image->source != IMA_SRC_TILED &&
                                           space->use_tile_drawing();
    void *lock;
    ImBuf *image_buffer = space->acquire_image_buffer(instance_data->image, &lock);
    const Scene *scene = DRW_context_state_get()->scene;
    instance_data->sh_params.update(space.get(), scene, instance_data->image, image_buffer);
    space->release_buffer(instance_data->image, image_buffer, lock);

    ImageUser *iuser = space->get_image_user();
    drawing_mode.cache_image(vedata, instance_data->image, iuser);
  }

  void draw_finish()
  {
    drawing_mode.draw_finish(vedata);

    IMAGE_InstanceData *instance_data = vedata->instance_data;
    instance_data->image = nullptr;
  }

  void draw_scene()
  {
    drawing_mode.draw_scene(vedata);
  }
};

/* -------------------------------------------------------------------- */
/** \name Engine Callbacks
 * \{ */

static void IMAGE_engine_init(void *ved)
{
  IMAGE_shader_library_ensure();
  IMAGE_Data *vedata = (IMAGE_Data *)ved;
  if (vedata->instance_data == nullptr) {
    vedata->instance_data = OBJECT_GUARDED_NEW(IMAGE_InstanceData);
  }
}

static void IMAGE_cache_init(void *vedata)
{
  const DRWContextState *draw_ctx = DRW_context_state_get();
  ImageEngine image_engine(draw_ctx, static_cast<IMAGE_Data *>(vedata));
  image_engine.cache_init();
  image_engine.cache_populate();
}

static void IMAGE_cache_populate(void *UNUSED(vedata), Object *UNUSED(ob))
{
  /* Function intentional left empty. `cache_populate` is required to be implemented. */
}

static void IMAGE_draw_scene(void *vedata)
{
  const DRWContextState *draw_ctx = DRW_context_state_get();
  ImageEngine image_engine(draw_ctx, static_cast<IMAGE_Data *>(vedata));
  image_engine.draw_scene();
  image_engine.draw_finish();
}

static void IMAGE_engine_free()
{
  IMAGE_shader_free();
}

static void IMAGE_instance_free(void *instance_data)
{
  OBJECT_GUARDED_DELETE(instance_data, IMAGE_InstanceData);
}

/** \} */

static const DrawEngineDataSize IMAGE_data_size = DRW_VIEWPORT_DATA_SIZE(IMAGE_Data);

}  // namespace blender::draw::image_engine

extern "C" {

using namespace blender::draw::image_engine;

DrawEngineType draw_engine_image_type = {
    nullptr,               /* next */
    nullptr,               /* prev */
    N_("UV/Image"),        /* idname */
    &IMAGE_data_size,      /* vedata_size */
    &IMAGE_engine_init,    /* engine_init */
    &IMAGE_engine_free,    /* engine_free */
    &IMAGE_instance_free,  /* instance_free */
    &IMAGE_cache_init,     /* cache_init */
    &IMAGE_cache_populate, /* cache_populate */
    nullptr,               /* cache_finish */
    &IMAGE_draw_scene,     /* draw_scene */
    nullptr,               /* view_update */
    nullptr,               /* id_update */
    nullptr,               /* render_to_image */
    nullptr,               /* store_metadata */
};
}
