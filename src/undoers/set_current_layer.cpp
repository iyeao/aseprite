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

#include "undoers/set_current_layer.h"

#include "raster/sprite.h"
#include "undo/objects_container.h"
#include "undo/undoers_collector.h"

using namespace undo;
using namespace undoers;

SetCurrentLayer::SetCurrentLayer(ObjectsContainer* objects, Sprite* sprite)
  : m_spriteId(objects->addObject(sprite))
  , m_layerId(sprite->getCurrentLayer() ? objects->addObject(sprite->getCurrentLayer()): 0)
{
}

void SetCurrentLayer::dispose()
{
  delete this;
}

void SetCurrentLayer::revert(ObjectsContainer* objects, UndoersCollector* redoers)
{
  Sprite* sprite = objects->getObjectT<Sprite>(m_spriteId);
  Layer* layer = (m_layerId ? objects->getObjectT<Layer>(m_layerId): NULL);

  // Push another SetCurrentLayer as redoer
  redoers->pushUndoer(new SetCurrentLayer(objects, sprite));

  sprite->setCurrentLayer(layer);
}
