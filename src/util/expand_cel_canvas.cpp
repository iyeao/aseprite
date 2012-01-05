/* ASE - Allegro Sprite Editor
 * Copyright (C) 2001-2011  David Capello
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "config.h"

#include "util/expand_cel_canvas.h"

#include "base/unique_ptr.h"
#include "document.h"
#include "raster/cel.h"
#include "raster/dirty.h"
#include "raster/layer.h"
#include "raster/sprite.h"
#include "raster/stock.h"
#include "undo/undo_history.h"
#include "undoers/add_cel.h"
#include "undoers/add_image.h"
#include "undoers/close_group.h"
#include "undoers/dirty_area.h"
#include "undoers/open_group.h"
#include "undoers/replace_image.h"
#include "undoers/set_cel_position.h"

ExpandCelCanvas::ExpandCelCanvas(Document* document, Sprite* sprite, Layer* layer, TiledMode tiledMode)
  : m_document(document)
  , m_sprite(sprite)
  , m_layer(layer)
  , m_cel(NULL)
  , m_celImage(NULL)
  , m_celCreated(false)
  , m_closed(false)
  , m_committed(false)
{
  if (m_layer->is_image()) {
    m_cel = static_cast<LayerImage*>(layer)->getCel(m_sprite->getCurrentFrame());
    if (m_cel)
      m_celImage = m_sprite->getStock()->getImage(m_cel->getImage());
  }

  // If there is no Cel
  if (m_cel == NULL) {
    // Create the image
    m_celImage = image_new(m_sprite->getImgType(), m_sprite->getWidth(), m_sprite->getHeight());
    image_clear(m_celImage, sprite->getTransparentColor());

    // create the cel
    m_cel = new Cel(m_sprite->getCurrentFrame(), 0);
    static_cast<LayerImage*>(m_layer)->addCel(m_cel);

    m_celCreated = true;
  }

  m_originalCelX = m_cel->getX();
  m_originalCelY = m_cel->getY();

  // region to draw
  int x1, y1, x2, y2;

  if (tiledMode == TILED_NONE) { // non-tiled
    x1 = MIN(m_cel->getX(), 0);
    y1 = MIN(m_cel->getY(), 0);
    x2 = MAX(m_cel->getX()+m_celImage->w, m_sprite->getWidth());
    y2 = MAX(m_cel->getY()+m_celImage->h, m_sprite->getHeight());
  }
  else {                        // tiled
    x1 = 0;
    y1 = 0;
    x2 = m_sprite->getWidth();
    y2 = m_sprite->getHeight();
  }

  // create two copies of the image region which we'll modify with the tool
  m_srcImage = image_crop(m_celImage,
                          x1-m_cel->getX(),
                          y1-m_cel->getY(), x2-x1, y2-y1,
                          m_sprite->getTransparentColor());

  m_dstImage = image_new_copy(m_srcImage);

  // We have to adjust the cel position to match the m_dstImage
  // position (the new m_dstImage will be used in RenderEngine to
  // draw this cel).
  m_cel->setPosition(x1, y1);
}

ExpandCelCanvas::~ExpandCelCanvas()
{
  try {
    if (!m_committed && !m_closed)
      rollback();
  }
  catch (...) {
    // Do nothing
  }
  delete m_srcImage;
  delete m_dstImage;
}

void ExpandCelCanvas::commit()
{
  ASSERT(!m_closed);
  ASSERT(!m_committed);

  undo::UndoHistory* undo = m_document->getUndoHistory();

  // If the size of each image is the same, we can create an undo
  // with only the differences between both images.
  if (m_cel->getX() == m_originalCelX &&
      m_cel->getY() == m_originalCelY &&
      m_celImage->w == m_dstImage->w &&
      m_celImage->h == m_dstImage->h) {
    // Was m_celImage created in the start of the tool-loop?.
    if (m_celCreated) {
      // We can keep the m_celImage

      // We copy the destination image to the m_celImage
      image_copy(m_celImage, m_dstImage, 0, 0);

      // Add the m_celImage in the images stock of the sprite.
      m_cel->setImage(m_sprite->getStock()->addImage(m_celImage));

      // Is the undo enabled?.
      if (undo->isEnabled()) {
        // We can temporary remove the cel.
        static_cast<LayerImage*>(m_sprite->getCurrentLayer())->removeCel(m_cel);

        // We create the undo information (for the new m_celImage
        // in the stock and the new cel in the layer)...
        undo->pushUndoer(new undoers::OpenGroup());
        undo->pushUndoer(new undoers::AddImage(undo->getObjects(),
                                               m_sprite->getStock(), m_cel->getImage()));
        undo->pushUndoer(new undoers::AddCel(undo->getObjects(),
                                             m_sprite->getCurrentLayer(), m_cel));
        undo->pushUndoer(new undoers::CloseGroup());

        // And finally we add the cel again in the layer.
        static_cast<LayerImage*>(m_sprite->getCurrentLayer())->addCel(m_cel);
      }
    }
    // If the m_celImage was already created before the whole process...
    else {
      // Add to the undo history the differences between m_celImage and m_dstImage
      if (undo->isEnabled()) {
        UniquePtr<Dirty> dirty(new Dirty(m_celImage, m_dstImage));

        dirty->saveImagePixels(m_celImage);
        if (dirty != NULL)
          undo->pushUndoer(new undoers::DirtyArea(undo->getObjects(), m_celImage, dirty));
      }

      // Copy the destination to the cel image.
      image_copy(m_celImage, m_dstImage, 0, 0);
    }
  }
  // If the size of both images are different, we have to
  // replace the entire image.
  else {
    if (undo->isEnabled()) {
      undo->pushUndoer(new undoers::OpenGroup());

      if (m_cel->getX() != m_originalCelX ||
          m_cel->getY() != m_originalCelY) {
        int x = m_cel->getX();
        int y = m_cel->getY();
        m_cel->setPosition(m_originalCelX, m_originalCelY);

        undo->pushUndoer(new undoers::SetCelPosition(undo->getObjects(), m_cel));

        m_cel->setPosition(x, y);
      }

      undo->pushUndoer(new undoers::ReplaceImage(undo->getObjects(),
                                                 m_sprite->getStock(), m_cel->getImage()));
      undo->pushUndoer(new undoers::CloseGroup());
    }

    // Replace the image in the stock.
    m_sprite->getStock()->replaceImage(m_cel->getImage(), m_dstImage);

    // Destroy the old cel image.
    image_free(m_celImage);

    // Now the m_dstImage is used, so we haven't to destroy it.
    m_dstImage = NULL;
  }

  m_committed = true;
}

void ExpandCelCanvas::rollback()
{
  ASSERT(!m_closed);
  ASSERT(!m_committed);

  // Here we destroy the temporary 'cel' created and restore all as it was before
  m_cel->setPosition(m_originalCelX, m_originalCelY);

  if (m_celCreated) {
    static_cast<LayerImage*>(m_layer)->removeCel(m_cel);
    delete m_cel;
    delete m_celImage;
  }

  m_closed = true;
}
