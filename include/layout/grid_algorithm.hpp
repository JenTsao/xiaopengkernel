#pragma once

#include "../css/computed_style.hpp"
#include "layout_box.hpp"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include <cmath>

namespace xiaopeng {
namespace layout {

// Grid item structure for layout calculation
struct GridItem {
  LayoutBoxPtr box;
  int columnStart = 1;
  int columnEnd = 2;
  int rowStart = 1;
  int rowEnd = 2;
  float contentWidth = 0.0f;
  float contentHeight = 0.0f;
};

// Track size info
struct TrackInfo {
  float minSize = 0.0f;
  float maxSize = 0.0f;
  float flexibleSize = 0.0f;
  float totalFr = 0.0f;
  float actualSize = 0.0f;
  float position = 0.0f;
  bool isAuto = true;
};

class GridAlgorithm {
public:
  void layoutGridContainer(LayoutBoxPtr container, const Dimensions& parentDim) {
    if (!container || container->type() != BoxType::BlockNode) {
      return;
    }

    const auto& style = container->style();
    if (style.display != css::Display::Grid) {
      return;
    }

    // 1. Calculate grid container dimensions
    calculateGridContainerDimensions(container, parentDim);

    // 2. Collect grid items
    std::vector<GridItem> items = collectGridItems(container);

    if (items.empty()) {
      return;
    }

    // 3. Determine grid dimensions
    auto [colCount, rowCount] = calculateGridDimensions(items, style);

    // 4. Calculate column and row sizes
    std::vector<TrackInfo> columnTracks;
    std::vector<TrackInfo> rowTracks;
    
    calculateTrackSizes(columnTracks, rowTracks, items, container, 
                        colCount, rowCount, style);

    // 5. Place grid items
    placeGridItems(items, columnTracks, rowTracks);

    // 6. Calculate container height if auto
    if (style.height.unit == css::Length::Unit::Auto) {
      calculateGridContainerHeight(container, rowTracks);
    }
  }

private:
  void calculateGridContainerDimensions(LayoutBoxPtr container, const Dimensions& parentDim) {
    const auto& style = container->style();

    // Width calculation
    float width = style.width.unit == css::Length::Unit::Auto
                  ? parentDim.content.width
                  : style.width.value;

    if (style.width.unit == css::Length::Unit::Percent) {
      width = parentDim.content.width * (style.width.value / 100.0f);
    }

    container->dimensions().content.width = std::max(0.0f, width);

    // Height calculation (auto initially, will be calculated later)
    if (style.height.unit == css::Length::Unit::Auto) {
      container->dimensions().content.height = 0; // Will be calculated later
    } else {
      container->dimensions().content.height = style.height.value;
    }

    // Copy padding/border/margin
    container->dimensions().padding.left = style.paddingLeft.value;
    container->dimensions().padding.right = style.paddingRight.value;
    container->dimensions().padding.top = style.paddingTop.value;
    container->dimensions().padding.bottom = style.paddingBottom.value;

    container->dimensions().border.left = style.borderLeftWidth.value;
    container->dimensions().border.right = style.borderRightWidth.value;
    container->dimensions().border.top = style.borderTopWidth.value;
    container->dimensions().border.bottom = style.borderBottomWidth.value;
  }

  std::vector<GridItem> collectGridItems(LayoutBoxPtr container) {
    std::vector<GridItem> items;

    for (const auto& child : container->children()) {
      if (!child) continue;

      const auto& childStyle = child->style();
      GridItem item;
      item.box = child;
      item.columnStart = std::max(1, childStyle.gridColumnStart);
      item.columnEnd = childStyle.gridColumnEnd > item.columnStart 
                       ? childStyle.gridColumnEnd 
                       : item.columnStart + 1;
      item.rowStart = std::max(1, childStyle.gridRowStart);
      item.rowEnd = childStyle.gridRowEnd > item.rowStart 
                   ? childStyle.gridRowEnd 
                   : item.rowStart + 1;

      items.push_back(std::move(item));
    }

    return items;
  }

  std::pair<int, int> calculateGridDimensions(const std::vector<GridItem>& items, 
                                               const css::ComputedStyle& style) {
    int maxColumn = 1;
    int maxRow = 1;

    for (const auto& item : items) {
      maxColumn = std::max(maxColumn, item.columnEnd);
      maxRow = std::max(maxRow, item.rowEnd);
    }

    // If we have template tracks, use that size if larger
    if (!style.gridTemplateColumns.empty()) {
      maxColumn = std::max(maxColumn, static_cast<int>(style.gridTemplateColumns.size()));
    }
    if (!style.gridTemplateRows.empty()) {
      maxRow = std::max(maxRow, static_cast<int>(style.gridTemplateRows.size()));
    }

    // Ensure minimum grid size
    if (maxColumn < 1) maxColumn = 1;
    if (maxRow < 1) maxRow = 1;

    return {maxColumn, maxRow};
  }

  void calculateTrackSizes(std::vector<TrackInfo>& columnTracks, 
                          std::vector<TrackInfo>& rowTracks,
                          const std::vector<GridItem>& items,
                          LayoutBoxPtr container,
                          int colCount, int rowCount,
                          const css::ComputedStyle& style) {
    
    // Initialize track info
    columnTracks.resize(colCount);
    rowTracks.resize(rowCount);

    float containerWidth = container->dimensions().content.width;
    float containerHeight = container->dimensions().content.height;
    float columnGap = style.gridColumnGap.value;
    float rowGap = style.gridRowGap.value;

    // Apply template columns if available
    if (!style.gridTemplateColumns.empty()) {
      int templateSize = static_cast<int>(style.gridTemplateColumns.size());
      for (int i = 0; i < std::min(templateSize, colCount); ++i) {
        auto size = style.gridTemplateColumns[i];
        switch (size.type) {
          case css::TrackSize::Type::Px:
            columnTracks[i].actualSize = size.value;
            columnTracks[i].isAuto = false;
            break;
          case css::TrackSize::Type::Percent:
            columnTracks[i].actualSize = containerWidth * (size.value / 100.0f);
            columnTracks[i].isAuto = false;
            break;
          case css::TrackSize::Type::Fr:
            columnTracks[i].flexibleSize = size.value;
            columnTracks[i].totalFr = size.value;
            columnTracks[i].isAuto = true;
            break;
          default:
            columnTracks[i].isAuto = true;
            break;
        }
      }
    }

    // Apply template rows if available
    if (!style.gridTemplateRows.empty()) {
      int templateSize = static_cast<int>(style.gridTemplateRows.size());
      for (int i = 0; i < std::min(templateSize, rowCount); ++i) {
        auto size = style.gridTemplateRows[i];
        switch (size.type) {
          case css::TrackSize::Type::Px:
            rowTracks[i].actualSize = size.value;
            rowTracks[i].isAuto = false;
            break;
          case css::TrackSize::Type::Percent:
            rowTracks[i].actualSize = containerHeight * (size.value / 100.0f);
            rowTracks[i].isAuto = false;
            break;
          case css::TrackSize::Type::Fr:
            rowTracks[i].flexibleSize = size.value;
            rowTracks[i].totalFr = size.value;
            rowTracks[i].isAuto = true;
            break;
          default:
            rowTracks[i].isAuto = true;
            break;
        }
      }
    }

    // Calculate column sizes - simplified auto-sizing
    calculateAutoTrackSizes(columnTracks, items, true);

    // Calculate row sizes - simplified auto-sizing
    calculateAutoTrackSizes(rowTracks, items, false);

    // Distribute flexible space (fr units)
    distributeFlexibleSpace(columnTracks, containerWidth, columnGap);
    distributeFlexibleSpace(rowTracks, containerHeight, rowGap);

    // Calculate track positions
    calculateTrackPositions(columnTracks, columnGap);
    calculateTrackPositions(rowTracks, rowGap);
  }

  void calculateAutoTrackSizes(std::vector<TrackInfo>& tracks,
                              const std::vector<GridItem>& items,
                              bool isColumns) {
    // For each auto track, calculate content-based size
    for (int i = 0; i < static_cast<int>(tracks.size()); ++i) {
      if (tracks[i].isAuto && tracks[i].flexibleSize == 0.0f) {
        float maxContentSize = 0.0f;
        
        for (const auto& item : items) {
          bool itemInTrack = false;
          if (isColumns) {
            itemInTrack = (item.columnStart <= i+1 && item.columnEnd > i+1);
          } else {
            itemInTrack = (item.rowStart <= i+1 && item.rowEnd > i+1);
          }

          if (itemInTrack && item.box) {
            // Get minimal content size - simplified
            if (isColumns) {
              const auto& itemStyle = item.box->style();
              if (itemStyle.width.unit != css::Length::Unit::Auto) {
                maxContentSize = std::max(maxContentSize, itemStyle.width.value);
              } else {
                maxContentSize = std::max(maxContentSize, 100.0f); // Default
              }
            } else {
              const auto& itemStyle = item.box->style();
              if (itemStyle.height.unit != css::Length::Unit::Auto) {
                maxContentSize = std::max(maxContentSize, itemStyle.height.value);
              } else {
                maxContentSize = std::max(maxContentSize, 50.0f); // Default
              }
            }
          }
        }
        tracks[i].actualSize = maxContentSize;
        tracks[i].minSize = maxContentSize;
      }
    }
  }

  void distributeFlexibleSpace(std::vector<TrackInfo>& tracks, 
                              float containerSize,
                              float gap) {
    // Calculate total fixed size and fr units
    float totalFixed = 0.0f;
    float totalFr = 0.0f;

    for (const auto& track : tracks) {
      if (track.flexibleSize > 0.0f) {
        totalFr += track.flexibleSize;
      } else {
        totalFixed += track.actualSize;
      }
    }

    // Add gap size
    totalFixed += gap * (tracks.size() - 1);

    // Distribute remaining space
    float remaining = std::max(0.0f, containerSize - totalFixed);

    if (totalFr > 0.0f && remaining > 0.0f) {
      float pxPerFr = remaining / totalFr;
      
      for (auto& track : tracks) {
        if (track.flexibleSize > 0.0f) {
          track.actualSize = track.flexibleSize * pxPerFr;
        }
      }
    }
  }

  void calculateTrackPositions(std::vector<TrackInfo>& tracks, float gap) {
    float position = 0.0f;
    for (int i = 0; i < static_cast<int>(tracks.size()); ++i) {
      tracks[i].position = position;
      position += tracks[i].actualSize;
      if (i < static_cast<int>(tracks.size()) - 1) {
        position += gap;
      }
    }
  }

  void placeGridItems(std::vector<GridItem>& items,
                      const std::vector<TrackInfo>& columnTracks,
                      const std::vector<TrackInfo>& rowTracks) {
    
    for (auto& item : items) {
      if (!item.box) continue;

      // Calculate item position and size
      int colStartIdx = std::max(0, item.columnStart - 1);
      int colEndIdx = std::min(static_cast<int>(columnTracks.size()), 
                                item.columnEnd - 1);
      int rowStartIdx = std::max(0, item.rowStart - 1);
      int rowEndIdx = std::min(static_cast<int>(rowTracks.size()), 
                                item.rowEnd - 1);

      if (colStartIdx >= colEndIdx || rowStartIdx >= rowEndIdx) {
        continue;
      }

      // Calculate position and size
      float x = columnTracks[colStartIdx].position;
      float y = rowTracks[rowStartIdx].position;
      
      float width = 0.0f;
      for (int i = colStartIdx; i < colEndIdx; ++i) {
        width += columnTracks[i].actualSize;
      }

      float height = 0.0f;
      for (int i = rowStartIdx; i < rowEndIdx; ++i) {
        height += rowTracks[i].actualSize;
      }

      // Set item dimensions
      auto& dims = item.box->dimensions();
      dims.content.x = x;
      dims.content.y = y;
      dims.content.width = width;
      dims.content.height = height;

      // Copy padding/border (item is already positioned)
      const auto& itemStyle = item.box->style();
      dims.padding.left = itemStyle.paddingLeft.value;
      dims.padding.right = itemStyle.paddingRight.value;
      dims.padding.top = itemStyle.paddingTop.value;
      dims.padding.bottom = itemStyle.paddingBottom.value;

      dims.border.left = itemStyle.borderLeftWidth.value;
      dims.border.right = itemStyle.borderRightWidth.value;
      dims.border.top = itemStyle.borderTopWidth.value;
      dims.border.bottom = itemStyle.borderBottomWidth.value;
    }
  }

  void calculateGridContainerHeight(LayoutBoxPtr container,
                                    const std::vector<TrackInfo>& rowTracks) {
    float totalHeight = 0.0f;
    float rowGap = container->style().gridRowGap.value;
    
    for (int i = 0; i < static_cast<int>(rowTracks.size()); ++i) {
      totalHeight += rowTracks[i].actualSize;
      if (i < static_cast<int>(rowTracks.size()) - 1) {
        totalHeight += rowGap;
      }
    }
    
    container->dimensions().content.height = totalHeight;
  }
};

} // namespace layout
} // namespace xiaopeng
