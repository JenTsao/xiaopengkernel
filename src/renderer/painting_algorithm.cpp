#include "renderer/painting_algorithm.hpp"
#include "dom/dom.hpp"
#include "loader/loader.hpp"
#include "renderer/image.hpp"
#include <algorithm>
#include <iostream>
#include <memory>

namespace xiaopeng {
namespace renderer {

void PaintingAlgorithm::paint(layout::LayoutBoxPtr root, Canvas &canvas) {
  if (!root)
    return;

  // Paint background
  canvas.clear(Color::White());

  auto rootLayer = buildLayerTree(root);
  paintLayer(rootLayer, canvas);
}

PaintLayerPtr PaintingAlgorithm::buildLayerTree(layout::LayoutBoxPtr root) {
  auto rootLayer = std::make_shared<PaintLayer>(root);
  collectLayers(root, rootLayer);
  return rootLayer;
}

void PaintingAlgorithm::collectLayers(layout::LayoutBoxPtr box, PaintLayerPtr currentLayer) {
  for (const auto &child : box->children()) {
    if (child->createsStackingContext()) {
      auto childLayer = std::make_shared<PaintLayer>(child);
      currentLayer->addStackingContextChild(childLayer);
      collectLayers(child, childLayer);
    } else {
      currentLayer->addNormalFlowChild(child);
      collectLayers(child, currentLayer);
    }
  }
}

void PaintingAlgorithm::getParentBorderBox(layout::LayoutBoxPtr box, int& px, int& py) {
  if (!box || !box->parent().lock()) {
    px = 0; py = 0; return;
  }
  auto parent = box->parent().lock();
  int ppx = 0, ppy = 0;
  getParentBorderBox(parent, ppx, ppy);
  
  const auto &pDims = parent->dimensions();
  int pContentAbsX = ppx + static_cast<int>(pDims.content.x);
  int pContentAbsY = ppy + static_cast<int>(pDims.content.y);
  px = pContentAbsX - static_cast<int>(pDims.padding.left) - static_cast<int>(pDims.border.left);
  py = pContentAbsY - static_cast<int>(pDims.padding.top) - static_cast<int>(pDims.border.top);
}

void PaintingAlgorithm::paintLayer(PaintLayerPtr layer, Canvas &canvas) {
  layer->sortNegZOrderList();
  layer->sortPosZOrderList();

  auto box = layer->box();
  int px = 0, py = 0;
  getParentBorderBox(box, px, py);

  // 1. Paint background and borders of this layer's element
  paintBox(box, canvas, px, py, PaintPhase::BackgroundAndBorders);

  // 2. Paint child layers with negative z-index
  for (const auto &childLayer : layer->negZOrderList()) {
    paintLayer(childLayer, canvas);
  }

  // 3. Paint normal flow children block backgrounds and borders
  for (const auto &childBox : layer->normalFlowChildren()) {
    if (!childBox->isFloat() && childBox->style().position == css::Position::Static) {
      paintNormalFlow(childBox, canvas, PaintPhase::BackgroundAndBorders);
    }
  }

  // 4. Paint floats
  for (const auto &childBox : layer->normalFlowChildren()) {
    if (childBox->isFloat()) {
      paintNormalFlow(childBox, canvas, PaintPhase::Both);
    }
  }

  // 5. Paint normal flow inline foregrounds (text and replaced elements)
  for (const auto &childBox : layer->normalFlowChildren()) {
    if (!childBox->isFloat() && childBox->style().position == css::Position::Static) {
      paintNormalFlow(childBox, canvas, PaintPhase::Foreground);
    }
  }

  // 6. Paint positioned elements with z-index auto or 0
  for (const auto &childBox : layer->normalFlowChildren()) {
    if (childBox->style().position != css::Position::Static) {
      paintNormalFlow(childBox, canvas, PaintPhase::Both);
    }
  }

  // 7. Paint foreground of this layer's element
  paintBox(box, canvas, px, py, PaintPhase::Foreground);

  // 8. Paint child layers with positive (or zero) z-index
  for (const auto &childLayer : layer->posZOrderList()) {
    paintLayer(childLayer, canvas);
  }
}

void PaintingAlgorithm::paintNormalFlow(layout::LayoutBoxPtr box, Canvas &canvas, PaintPhase phase) {
  int px = 0, py = 0;
  getParentBorderBox(box, px, py);
  paintBox(box, canvas, px, py, phase);
}

void PaintingAlgorithm::paintBox(layout::LayoutBoxPtr box, Canvas &canvas,
                                 int parentBorderBoxX, int parentBorderBoxY, PaintPhase phase) {
  const auto &dims = box->dimensions();
  const auto &style = box->style();

  // Helper: convert css::Color to renderer::Color
  auto toColor = [](const css::Color &c) -> Color {
    return {c.r, c.g, c.b, c.a};
  };

  // Content box absolute position (relative to canvas)
  int contentAbsX = parentBorderBoxX + static_cast<int>(dims.content.x);
  int contentAbsY = parentBorderBoxY + static_cast<int>(dims.content.y);

  // Border box absolute position (derived from content box)
  int borderBoxX = contentAbsX - static_cast<int>(dims.padding.left) -
                   static_cast<int>(dims.border.left);
  int borderBoxY = contentAbsY - static_cast<int>(dims.padding.top) -
                   static_cast<int>(dims.border.top);

  int borderBoxW = static_cast<int>(dims.border.left + dims.padding.left +
                                    dims.content.width + dims.padding.right +
                                    dims.border.right);
  int borderBoxH = static_cast<int>(dims.border.top + dims.padding.top +
                                    dims.content.height + dims.padding.bottom +
                                    dims.border.bottom);

  // Viewport bounds clipping: skip boxes entirely outside the canvas
  if (borderBoxX + borderBoxW <= 0 || borderBoxY + borderBoxH <= 0 ||
      borderBoxX >= canvas.width() || borderBoxY >= canvas.height()) {
    return;
  }

  // --- 0. Check Overflow and push clip rect if needed ---
  bool needsClip = false;
  if (style.overflowX != css::Overflow::Visible ||
      style.overflowY != css::Overflow::Visible) {
    // Clip to padding box (content + padding, excluding border)
    int clipX = borderBoxX + static_cast<int>(dims.border.left);
    int clipY = borderBoxY + static_cast<int>(dims.border.top);
    int clipW = static_cast<int>(dims.padding.left + dims.content.width +
                                 dims.padding.right);
    int clipH = static_cast<int>(dims.padding.top + dims.content.height +
                                 dims.padding.bottom);
    canvas.pushClipRect(clipX, clipY, clipW, clipH);
    needsClip = true;
  }

  // --- 1. Draw Background (full border box area) ---
  if ((phase == PaintPhase::BackgroundAndBorders || phase == PaintPhase::Both) && style.backgroundColor.a > 0) {
    int bgX = borderBoxX + static_cast<int>(dims.border.left);
    int bgY = borderBoxY + static_cast<int>(dims.border.top);
    int bgW = static_cast<int>(dims.padding.left + dims.content.width +
                               dims.padding.right);
    int bgH = static_cast<int>(dims.padding.top + dims.content.height +
                               dims.padding.bottom);
    canvas.fillRect(bgX, bgY, bgW, bgH, toColor(style.backgroundColor));
  }

  // --- 2. Draw Borders ---
  // Note: borders are drawn even if overflow: hidden, so we need to temporarily pop clip
  if (needsClip) {
    canvas.popClipRect();
  }

  if (phase == PaintPhase::BackgroundAndBorders || phase == PaintPhase::Both) {
    // Top Border
    if (dims.border.top > 0 && style.borderTopColor.a > 0) {
      canvas.fillRect(borderBoxX, borderBoxY, borderBoxW,
                      static_cast<int>(dims.border.top),
                      toColor(style.borderTopColor));
    }

    // Bottom Border
    if (dims.border.bottom > 0 && style.borderBottomColor.a > 0) {
      int by = borderBoxY + borderBoxH - static_cast<int>(dims.border.bottom);
      canvas.fillRect(borderBoxX, by, borderBoxW,
                      static_cast<int>(dims.border.bottom),
                      toColor(style.borderBottomColor));
    }

    // Left Border
    if (dims.border.left > 0 && style.borderLeftColor.a > 0) {
      canvas.fillRect(borderBoxX, borderBoxY, static_cast<int>(dims.border.left),
                      borderBoxH, toColor(style.borderLeftColor));
    }

    // Right Border
    if (dims.border.right > 0 && style.borderRightColor.a > 0) {
      int rx = borderBoxX + borderBoxW - static_cast<int>(dims.border.right);
      canvas.fillRect(rx, borderBoxY, static_cast<int>(dims.border.right),
                      borderBoxH, toColor(style.borderRightColor));
    }
  }

  // Re-push clip if needed for content
  if (needsClip) {
    // Clip to padding box
    int clipX = borderBoxX + static_cast<int>(dims.border.left);
    int clipY = borderBoxY + static_cast<int>(dims.border.top);
    int clipW = static_cast<int>(dims.padding.left + dims.content.width +
                                 dims.padding.right);
    int clipH = static_cast<int>(dims.padding.top + dims.content.height +
                                 dims.padding.bottom);
    canvas.pushClipRect(clipX, clipY, clipW, clipH);
  }

  // --- 3. Paint Block Children ---
  // NOTE: Children are now collected and sorted globally,
  // so we don't recursively paint children here.

  if (phase == PaintPhase::Foreground || phase == PaintPhase::Both) {
    // --- 3.5. Paint Replaced Elements (Images) ---
    if (box->node() && box->node()->nodeType() == dom::NodeType::Element) {
      auto elem = std::dynamic_pointer_cast<dom::Element>(box->node());
      if (elem && elem->localName() == "img") {
        auto attr = elem->getAttribute("src");
        if (attr.has_value() && !attr.value().empty()) {
          std::string src = attr.value();

          // Cache lookup — avoids per-frame network download + decode
          std::shared_ptr<Image> image;
          auto cacheIt = imageCache_.find(src);
          if (cacheIt != imageCache_.end()) {
            image = cacheIt->second;
          } else {
            image = loader::GetLoader().loadImage(src);
            if (image) {
              imageCache_[src] = image;
              std::cout << "[Painter] Cached image: " << src << " ("
                        << image->width() << "x" << image->height() << ")"
                        << std::endl;
            }
          }

          if (image) {
            int imgX = borderBoxX + static_cast<int>(dims.border.left) +
                       static_cast<int>(dims.padding.left);
            int imgY = borderBoxY + static_cast<int>(dims.border.top) +
                       static_cast<int>(dims.padding.top);
            canvas.drawImage(*image, imgX, imgY, static_cast<int>(dims.content.width),
                             static_cast<int>(dims.content.height));
          }
        }
      }
    }

    // --- 4. Paint Inline Text Lines ---
    if (!box->lineBoxes().empty()) {
      for (const auto &line : box->lineBoxes()) {
        int lineAbsY = borderBoxY + static_cast<int>(line.y());

        for (const auto &frag : line.fragments()) {
          if (!frag.box || !frag.isText)
            continue;

          int fragAbsX = borderBoxX + static_cast<int>(dims.border.left) +
                         static_cast<int>(dims.padding.left) +
                         static_cast<int>(frag.x);
          int fragAbsY = lineAbsY + static_cast<int>(frag.y);

          const std::string &textContent = frag.box->node()->textContent();
          if (frag.startOffset < textContent.length()) {
            std::string text = textContent.substr(
                frag.startOffset, frag.endOffset - frag.startOffset);

            const auto &c = frag.box->style().color;

            // Determine scale for fallback or if font size is relative
            int scale = 1;
            if (frag.box->node()->parentNode() &&
                frag.box->node()->parentNode()->nodeType() ==
                    dom::NodeType::Element) {
              auto parent = std::dynamic_pointer_cast<dom::Element>(
                  frag.box->node()->parentNode());
              if (parent && parent->localName() == "h1") {
                scale = 2;
              }
            }

            if (frag.box->style().fontSize.unit == css::Length::Unit::Px) {
              canvas.setFont(frag.box->style().fontFamily,
                             static_cast<int>(frag.box->style().fontSize.value));
            } else {
              // Fallback default
              canvas.setFont(frag.box->style().fontFamily, 16 * scale);
            }

            canvas.drawTextVector(fragAbsX, fragAbsY, text, toColor(c));
          }
        }
      }

      // --- 4b. Paint text decorations (underline, overline, line-through) ---
      for (const auto &line : box->lineBoxes()) {
        int lineAbsY = borderBoxY + static_cast<int>(line.y());

        for (const auto &frag : line.fragments()) {
          if (!frag.box || !frag.isText)
            continue;
          if (frag.textDecorationLine == css::TextDecorationLine::None)
            continue;

          int fragAbsX = borderBoxX + static_cast<int>(dims.border.left) +
                         static_cast<int>(dims.padding.left) +
                         static_cast<int>(frag.x);
          int fragAbsY = lineAbsY + static_cast<int>(frag.y);
          int fragW = static_cast<int>(frag.width);
          int fragH = static_cast<int>(frag.height);
          int baselineY = fragAbsY + static_cast<int>(frag.baseline);

          const auto &c = frag.box->style().color;
          Color decoColor = toColor(c);

          switch (frag.textDecorationLine) {
            case css::TextDecorationLine::Underline:
              // 1px line below baseline
              canvas.drawLine(fragAbsX, baselineY + 2,
                              fragAbsX + fragW, baselineY + 2, decoColor);
              break;
            case css::TextDecorationLine::Overline:
              // 1px line at top of fragment
              canvas.drawLine(fragAbsX, fragAbsY,
                              fragAbsX + fragW, fragAbsY, decoColor);
              break;
            case css::TextDecorationLine::LineThrough:
              // 1px line at middle of fragment
              canvas.drawLine(fragAbsX, fragAbsY + fragH / 2,
                              fragAbsX + fragW, fragAbsY + fragH / 2, decoColor);
              break;
            default:
              break;
          }
        }
      }
    }
  }

  // --- 5. Pop clip rect if we pushed one ---
  if (needsClip) {
    canvas.popClipRect();
  }
}

Color PaintingAlgorithm::getColorForBox(layout::LayoutBoxPtr box) {
  if (!box->node())
    return Color::Transparent();
  if (box->node()->nodeType() != dom::NodeType::Element)
    return Color::Transparent();

  auto elem = std::dynamic_pointer_cast<dom::Element>(box->node());
  if (!elem)
    return Color::Transparent();
  if (elem->localName() == "div")
    return {200, 200, 200, 255};
  if (elem->localName() == "p")
    return {200, 200, 255, 255};
  if (elem->localName() == "h1")
    return {255, 200, 200, 255};

  return Color::Transparent();
}

} // namespace renderer
} // namespace xiaopeng
