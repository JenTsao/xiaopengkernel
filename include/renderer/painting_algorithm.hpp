#pragma once

#include "layout/layout_box.hpp"
#include "renderer/canvas.hpp"
#include "renderer/image.hpp"
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "renderer/paint_layer.hpp"

namespace xiaopeng {
namespace renderer {

enum class PaintPhase {
  BackgroundAndBorders,
  Foreground,
  Both
};

class PaintingAlgorithm {
public:
  void paint(layout::LayoutBoxPtr root, Canvas &canvas);

  /// Document URL used to resolve relative <img src> values.
  void setBaseUrl(const std::string &url) { baseUrl_ = url; }

  /// Clear the decoded-image cache (call on page reload / navigation).
  void clearImageCache() {
    imageCache_.clear();
    failedImages_.clear();
  }

private:
  PaintLayerPtr buildLayerTree(layout::LayoutBoxPtr root);
  void collectLayers(layout::LayoutBoxPtr box, PaintLayerPtr currentLayer);
  
  void paintLayer(PaintLayerPtr layer, Canvas &canvas);
  void paintNormalFlow(layout::LayoutBoxPtr box, Canvas &canvas, PaintPhase phase);
  void paintBox(layout::LayoutBoxPtr box, Canvas &canvas, int parentBorderBoxX, int parentBorderBoxY, PaintPhase phase);
  void getParentBorderBox(layout::LayoutBoxPtr box, int& px, int& py);

  // Heuristic to determine color based on element type/class for visualization
  Color getColorForBox(layout::LayoutBoxPtr box);

  // Image cache: URL → decoded Image (avoids per-frame re-download/decode)
  std::unordered_map<std::string, std::shared_ptr<Image>> imageCache_;
  // Sources that failed to load; retrying them every frame would block the
  // main thread on the network timeout each time
  std::unordered_set<std::string> failedImages_;
  std::string baseUrl_;
};

} // namespace renderer
} // namespace xiaopeng
