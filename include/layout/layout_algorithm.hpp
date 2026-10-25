#pragma once

#include "layout_box.hpp"
#include "renderer/text_metrics.hpp"
#include "flexbox_algorithm.hpp"
#include "grid_algorithm.hpp"
#include "inline_formatting_context.hpp"
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace xiaopeng {
namespace layout {

class InlineFormattingContext;

class LayoutAlgorithm {
public:
  // The IFC owns the inline layout strategy and needs access to the
  // private block-layout helpers below to size atomic inline-level boxes.
  friend class InlineFormattingContext;
  void layout(LayoutBoxPtr root, const Dimensions &container) {
    if (!root)
      return;

    // Initial containing block
    root->dimensions().content = container.content;
    root->dimensions().content.x = 0;
    root->dimensions().content.y = 0;

    layoutBox(root, container);
  }

private:
  void layoutBox(LayoutBoxPtr box, const Dimensions &parentDim) {
    // Check if this is an absolutely or fixed positioned element
    if (box->style().position == css::Position::Absolute ||
        box->style().position == css::Position::Fixed) {
      // Handle positioning in layoutBlock, but still need to calculate
      // proper width/height and layout children
      calculateBlockWidth(box, parentDim);
      
      // Layout children normally (they are in flow of this positioned container)
      if (box->style().display == css::Display::Flex) {
        FlexboxAlgorithm flexbox;
        flexbox.layoutFlexContainer(box, parentDim);
      } else if (box->style().display == css::Display::Grid) {
        GridAlgorithm grid;
        grid.layoutGridContainer(box, parentDim);
      } else if (isInlineFormattingContext(box)) {
        layoutInline(box);
      } else {
        layoutBlock(box);
      }
      
      // Calculate height for non-flex and non-grid containers
      if (box->style().display != css::Display::Flex &&
          box->style().display != css::Display::Grid) {
        calculateBlockHeight(box);
      }
      return;
    }

    // 1. Calculate Width
    calculateBlockWidth(box, parentDim);

    // 2. Determine Layout Mode (BFC, IFC, or Flexbox)
    if (box->type() == BoxType::BlockNode ||
        box->type() == BoxType::AnonymousBlock) {
      
      // Check if this is a flex container
      if (box->style().display == css::Display::Flex) {
        // Use Flexbox layout algorithm
        FlexboxAlgorithm flexbox;
        flexbox.layoutFlexContainer(box, parentDim);
      } else if (box->style().display == css::Display::Grid) {
        // Use Grid layout algorithm
        GridAlgorithm grid;
        grid.layoutGridContainer(box, parentDim);
      } else if (isInlineFormattingContext(box)) {
        layoutInline(box);
      } else {
        layoutBlock(box);
      }
    } else if (box->type() == BoxType::InlineBlockNode) {
      // Inline-block: behave like a block internally but inline in flow
      // First calculate width/height like a block
      calculateBlockWidth(box, parentDim);
      
      // Layout children like a regular block
      layoutBlock(box);
      
      // Calculate final height
      calculateBlockHeight(box);
    } else if (box->type() == BoxType::InlineNode ||
               box->type() == BoxType::AnonymousInline) {
      // Inline nodes are typically handled by their parent's IFC.
    }

    // 3. Calculate Height (for non-flex and non-grid containers)
    if (box->style().display != css::Display::Flex &&
        box->style().display != css::Display::Grid) {
      calculateBlockHeight(box);
    }
  }

  LayoutBoxPtr findContainingBlock(LayoutBoxPtr box) {
    LayoutBoxPtr current = box;
    while (current) {
      auto parent = current->parent().lock();
      if (!parent) break;
      
      auto &style = parent->style();
      if (style.position != css::Position::Static) {
        return parent;
      }
      
      current = parent;
    }
    return nullptr;
  }

  void layoutAbsolutePositioned(LayoutBoxPtr parent) {
    for (auto &child : parent->children()) {
      if (child->style().position == css::Position::Absolute) {
        layoutAbsoluteBox(child, parent);
      } else if (child->style().position == css::Position::Fixed) {
        layoutFixedBox(child);
      }
    }
  }

  void layoutAbsoluteBox(LayoutBoxPtr box, LayoutBoxPtr containingBlock) {
    Dimensions cbDim;
    if (containingBlock) {
      cbDim = containingBlock->dimensions();
      // For containing block, we use its padding box as reference
    } else {
      cbDim.content = {0, 0, 800, 600};
    }

    const auto &style = box->style();
    auto &dims = box->dimensions();

    // --- Calculate width ---
    float width = 0.0f;
    if (style.width.unit != css::Length::Unit::Auto) {
      if (style.width.unit == css::Length::Unit::Percent) {
        width = cbDim.content.width * (style.width.value / 100.0f);
      } else {
        width = style.width.value;
      }
    } else {
      // If width auto, use shrink-to-fit or available
      width = cbDim.content.width * 0.8f; // Default fallback
    }

    // Apply box-sizing
    float paddingH = style.paddingLeft.value + style.paddingRight.value;
    float borderH = style.borderLeftWidth.value + style.borderRightWidth.value;
    if (style.boxSizing == css::BoxSizing::BorderBox) {
      width = std::max(0.0f, width - paddingH - borderH);
    }

    dims.content.width = std::max(0.0f, width);

    // --- Calculate horizontal position ---
    float left = 0.0f;
    if (style.left.unit == css::Length::Unit::Percent) {
      left = cbDim.content.width * (style.left.value / 100.0f);
    } else if (style.left.unit != css::Length::Unit::Auto) {
      left = style.left.value;
    } else if (style.right.unit != css::Length::Unit::Auto) {
      // If right specified and left auto, calculate from right
      float right = style.right.unit == css::Length::Unit::Percent ? 
                    cbDim.content.width * (style.right.value / 100.0f) : 
                    style.right.value;
      left = cbDim.content.width - dims.content.width - paddingH - borderH - right;
    }

    // Position content box
    dims.content.x = cbDim.content.x + left + style.marginLeft.value + 
                     style.borderLeftWidth.value + style.paddingLeft.value;

    // --- Calculate height (use already calculated height from layoutBox) ---
    // If height is auto, keep the calculated value from child layout
    // Otherwise, apply box sizing and constraints
    float height = dims.content.height;
    if (style.height.unit != css::Length::Unit::Auto) {
      if (style.height.unit == css::Length::Unit::Percent) {
        height = cbDim.content.height * (style.height.value / 100.0f);
      } else {
        height = style.height.value;
      }

      float paddingV = style.paddingTop.value + style.paddingBottom.value;
      float borderV = style.borderTopWidth.value + style.borderBottomWidth.value;
      if (style.boxSizing == css::BoxSizing::BorderBox) {
        height = std::max(0.0f, height - paddingV - borderV);
      }
      dims.content.height = std::max(0.0f, height);
    }

    // --- Calculate vertical position ---
    float top = 0.0f;
    if (style.top.unit == css::Length::Unit::Percent) {
      top = cbDim.content.height * (style.top.value / 100.0f);
    } else if (style.top.unit != css::Length::Unit::Auto) {
      top = style.top.value;
    } else if (style.bottom.unit != css::Length::Unit::Auto) {
      // If bottom specified and top auto, calculate from bottom
      float bottom = style.bottom.unit == css::Length::Unit::Percent ? 
                     cbDim.content.height * (style.bottom.value / 100.0f) : 
                     style.bottom.value;
      top = cbDim.content.height - dims.content.height - (style.paddingTop.value + style.paddingBottom.value) - 
            (style.borderTopWidth.value + style.borderBottomWidth.value) - bottom;
    }

    dims.content.y = cbDim.content.y + top + style.marginTop.value +
                     style.borderTopWidth.value + style.paddingTop.value;

    // Ensure padding/border/margin are set
    dims.padding.top = style.paddingTop.value;
    dims.padding.right = style.paddingRight.value;
    dims.padding.bottom = style.paddingBottom.value;
    dims.padding.left = style.paddingLeft.value;

    dims.border.top = style.borderTopWidth.value;
    dims.border.right = style.borderRightWidth.value;
    dims.border.bottom = style.borderBottomWidth.value;
    dims.border.left = style.borderLeftWidth.value;

    dims.margin.top = style.marginTop.value;
    dims.margin.right = style.marginRight.value;
    dims.margin.bottom = style.marginBottom.value;
    dims.margin.left = style.marginLeft.value;
  }

  void layoutFixedBox(LayoutBoxPtr box) {
    LayoutBoxPtr viewportBox = nullptr;
    layoutAbsoluteBox(box, viewportBox);
  }

  bool isInlineFormattingContext(LayoutBoxPtr box) {
    bool hasInline = false;
    for (auto child : box->children()) {
      // Float elements are inline-level but formatted in BFC context.
      // They don't prevent IFC formation on the parent.
      if (child->style().cssFloat != css::Float::None)
        continue;

      if (child->type() == BoxType::BlockNode ||
          child->type() == BoxType::AnonymousBlock) {
        return false; // Contains block, so it's a BFC
      }
      // inline-block is an atomic inline-level box (CSS §9.2.4): it
      // participates in the inline formatting context just like text and
      // other inline-level boxes.
      if (child->type() == BoxType::InlineNode ||
          child->type() == BoxType::AnonymousInline ||
          child->type() == BoxType::InlineBlockNode ||
          (child->node() && child->node()->nodeType() == dom::NodeType::Text)) {
        hasInline = true;
      }
    }
    return hasInline;
  }

  // Legacy member kept for backward compat with IFC computeFloatInsets
  std::vector<LayoutBoxPtr> activeFloats_;

  /// Find the lowest Y where a new float can be placed without overlapping.
  float findFloatPosition(const std::vector<FloatInfo> &floats,
                          float containerWidth, float newW, float newH,
                          float startY, bool isLeft) const {
    float y = startY;
    for (int iter = 0; iter < 50; ++iter) {
      bool fits = true;
      for (const auto &f : floats) {
        if (y < f.y + f.height && y + newH > f.y) {
          if (isLeft) {
            if (newW > f.x && 0 < f.x + f.width) {
              y = f.y + f.height;
              fits = false;
              break;
            }
          } else {
            float fLeft = containerWidth - f.width;
            float newLeft = containerWidth - newW;
            if (newLeft < fLeft + f.width && newLeft + newW > fLeft) {
              y = f.y + f.height;
              fits = false;
              break;
            }
          }
        }
      }
      if (fits)
        return y;
    }
    return y;
  }

  /// Advance currentY past all floats matching the clear direction.
  float clearFloats(float currentY,
                    const std::vector<FloatInfo> &leftFloats,
                    const std::vector<FloatInfo> &rightFloats,
                    css::Clear clear) const {
    float clearY = currentY;
    if (clear == css::Clear::Left || clear == css::Clear::Both) {
      for (const auto &f : leftFloats) {
        float bottom = f.y + f.height;
        if (bottom > clearY)
          clearY = bottom;
      }
    }
    if (clear == css::Clear::Right || clear == css::Clear::Both) {
      for (const auto &f : rightFloats) {
        float bottom = f.y + f.height;
        if (bottom > clearY)
          clearY = bottom;
      }
    }
    return clearY;
  }

  void layoutBlock(LayoutBoxPtr box) {
    float currentY = 0;
    std::vector<FloatInfo> leftFloats;
    std::vector<FloatInfo> rightFloats;

    for (auto child : box->children()) {
      if (child->style().position == css::Position::Absolute ||
          child->style().position == css::Position::Fixed) {
        continue;
      }

      // Handle clear: advance currentY past the relevant floats
      if (child->style().clear != css::Clear::None) {
        currentY = clearFloats(currentY, leftFloats, rightFloats,
                               child->style().clear);
      }

      layoutBox(child, box->dimensions());

      float childOuterWidth =
          child->dimensions().margin.left + child->dimensions().border.left +
          child->dimensions().padding.left + child->dimensions().content.width +
          child->dimensions().padding.right + child->dimensions().border.right +
          child->dimensions().margin.right;
      float childOuterHeight =
          child->dimensions().margin.top + child->dimensions().border.top +
          child->dimensions().padding.top + child->dimensions().content.height +
          child->dimensions().padding.bottom +
          child->dimensions().border.bottom +
          child->dimensions().margin.bottom;

      float parentContentX = box->dimensions().padding.left +
                             box->dimensions().border.left;
      float parentContentY = box->dimensions().padding.top +
                             box->dimensions().border.top;

      if (child->style().cssFloat == css::Float::Left) {
        float floatY = findFloatPosition(leftFloats,
                                         box->dimensions().content.width,
                                         childOuterWidth, childOuterHeight,
                                         currentY, true);

        child->dimensions().content.x =
            parentContentX + child->dimensions().margin.left;
        child->dimensions().content.y =
            parentContentY + floatY + child->dimensions().margin.top;
        child->setFloat(true);

        FloatInfo fi;
        fi.box = child;
        fi.x = child->dimensions().content.x - child->dimensions().margin.left;
        fi.y = child->dimensions().content.y - child->dimensions().margin.top;
        fi.width = childOuterWidth;
        fi.height = childOuterHeight;
        fi.isLeft = true;
        leftFloats.push_back(fi);

      } else if (child->style().cssFloat == css::Float::Right) {
        float floatY = findFloatPosition(rightFloats,
                                         box->dimensions().content.width,
                                         childOuterWidth, childOuterHeight,
                                         currentY, false);

        child->dimensions().content.x =
            parentContentX + box->dimensions().content.width -
            childOuterWidth + child->dimensions().margin.left;
        child->dimensions().content.y =
            parentContentY + floatY + child->dimensions().margin.top;
        child->setFloat(true);

        FloatInfo fi;
        fi.box = child;
        fi.x = child->dimensions().content.x - child->dimensions().margin.left;
        fi.y = child->dimensions().content.y - child->dimensions().margin.top;
        fi.width = childOuterWidth;
        fi.height = childOuterHeight;
        fi.isLeft = false;
        rightFloats.push_back(fi);

      } else {
        // Normal flow child
        child->dimensions().content.x =
            parentContentX + child->dimensions().margin.left;
        child->dimensions().content.y =
            parentContentY + currentY + child->dimensions().margin.top;

        currentY += childOuterHeight;
      }
    }

    for (auto child : box->children()) {
      if (child->style().position == css::Position::Absolute) {
        LayoutBoxPtr containingBlock = findContainingBlock(child);
        layoutAbsoluteBox(child, containingBlock);
      } else if (child->style().position == css::Position::Fixed) {
        layoutFixedBox(child);
      }
    }

    // Store float lists on the box for IFC and height calculation
    box->setActiveFloats(leftFloats, rightFloats);

    // Update legacy activeFloats_ for IFC backward compat
    activeFloats_.clear();
    for (const auto &fi : leftFloats)
      activeFloats_.push_back(fi.box);
    for (const auto &fi : rightFloats)
      activeFloats_.push_back(fi.box);

    sortByZIndex(box);
  }

  void sortByZIndex(LayoutBoxPtr box) {
    // If this box creates a stacking context, sort its children
    if (!box->createsStackingContext()) {
      return;
    }

    // Create a list of children with their stacking info
    struct StackingInfo {
      LayoutBoxPtr box;
      int zIndex;
      bool createsSC;
      size_t originalIndex;
    };

    std::vector<StackingInfo> childrenWithZ;
    size_t index = 0;
    for (auto child : box->children()) {
      childrenWithZ.push_back({
        child,
        child->style().zIndex,
        child->createsStackingContext(),
        index++
      });
    }

    // Sort by z-index, with special handling for stacking context creators
    std::sort(childrenWithZ.begin(), childrenWithZ.end(),
              [](const StackingInfo &a, const StackingInfo &b) {
                // Elements with explicit z-index come first
                if (a.zIndex != b.zIndex) {
                  return a.zIndex < b.zIndex;
                }
                // For equal z-index: stacking context creators first, then DOM order
                if (a.createsSC != b.createsSC) {
                  return a.createsSC;
                }
                // Fallback to DOM order (lower index comes first)
                return a.originalIndex < b.originalIndex;
              });

    // Reorder the children vector
    std::vector<LayoutBoxPtr> sortedChildren;
    for (const auto &info : childrenWithZ) {
      sortedChildren.push_back(info.box);
    }

    // Update the box's children
    // We need to update the underlying vector in the LayoutBox
    // This requires modifying the children_ vector directly
    // Since we don't have direct access, we need to clear and re-add
    // (This is a limitation of the current design)
    
    // Note: The current LayoutBox doesn't expose direct access to children_
    // For now, we'll assume the painter/renderer handles z-index ordering
  }

  void collectInlineItems(LayoutBoxPtr box, std::vector<LayoutBoxPtr> &items) {
    // Retained for backwards compatibility with any external callers; the
    // live implementation now lives in InlineFormattingContext.
    for (auto child : box->children()) {
      if (child->node() &&
          child->node()->nodeType() == dom::NodeType::Text) {
        items.push_back(child);
        continue;
      }
      if (child->type() == BoxType::InlineNode || child->type() == BoxType::AnonymousInline) {
        collectInlineItems(child, items);
      } else if (child->type() == BoxType::InlineBlockNode) {
        items.push_back(child);
      }
    }
  }

  void layoutInline(LayoutBoxPtr box) {
    // 1. Process and position float children within this IFC
    std::vector<FloatInfo> leftFloats = box->leftFloats();
    std::vector<FloatInfo> rightFloats = box->rightFloats();

    float currentY = 0; // Relative to content top
    for (auto child : box->children()) {
      if (child->style().position == css::Position::Absolute ||
          child->style().position == css::Position::Fixed) {
        continue;
      }
      if (child->style().cssFloat != css::Float::None) {
        layoutBox(child, box->dimensions());
        float childOuterWidth = child->dimensions().margin.left + child->dimensions().border.left +
                                child->dimensions().padding.left + child->dimensions().content.width +
                                child->dimensions().padding.right + child->dimensions().border.right +
                                child->dimensions().margin.right;
        float childOuterHeight = child->dimensions().margin.top + child->dimensions().border.top +
                                 child->dimensions().padding.top + child->dimensions().content.height +
                                 child->dimensions().padding.bottom + child->dimensions().border.bottom +
                                 child->dimensions().margin.bottom;
                                 
        float parentContentX = box->dimensions().padding.left + box->dimensions().border.left;
        float parentContentY = box->dimensions().padding.top + box->dimensions().border.top;

        if (child->style().cssFloat == css::Float::Left) {
          float floatY = findFloatPosition(leftFloats, box->dimensions().content.width, childOuterWidth, childOuterHeight, currentY, true);
          child->dimensions().content.x = parentContentX + child->dimensions().margin.left;
          child->dimensions().content.y = parentContentY + floatY + child->dimensions().margin.top;
          child->setFloat(true);
          FloatInfo fi;
          fi.box = child;
          fi.x = child->dimensions().content.x - child->dimensions().margin.left;
          fi.y = child->dimensions().content.y - child->dimensions().margin.top;
          fi.width = childOuterWidth;
          fi.height = childOuterHeight;
          fi.isLeft = true;
          leftFloats.push_back(fi);
        } else if (child->style().cssFloat == css::Float::Right) {
          float floatY = findFloatPosition(rightFloats, box->dimensions().content.width, childOuterWidth, childOuterHeight, currentY, false);
          child->dimensions().content.x = parentContentX + box->dimensions().content.width - childOuterWidth + child->dimensions().margin.left;
          child->dimensions().content.y = parentContentY + floatY + child->dimensions().margin.top;
          child->setFloat(true);
          FloatInfo fi;
          fi.box = child;
          fi.x = child->dimensions().content.x - child->dimensions().margin.left;
          fi.y = child->dimensions().content.y - child->dimensions().margin.top;
          fi.width = childOuterWidth;
          fi.height = childOuterHeight;
          fi.isLeft = false;
          rightFloats.push_back(fi);
        }
      }
    }
    box->setActiveFloats(leftFloats, rightFloats);

    // Pass the combined BFC's float lists to the IFC so it can size lines
    // around intruding floats.
    InlineFormattingContext ifc(leftFloats, rightFloats);
    ifc.layout(*this, box);
  }

  void calculateBlockWidth(LayoutBoxPtr box, const Dimensions &parentDim) {
    const auto &style = box->style();

    float width = 0.0f;

    // Parse width
    if (style.width.unit == css::Length::Unit::Auto) {
      if (box->type() == BoxType::InlineBlockNode || style.cssFloat != css::Float::None ||
          style.position == css::Position::Absolute || style.position == css::Position::Fixed) {
          // Shrink-to-fit approximation: use the available width. A real
          // implementation would need a max-content pre-pass.
          width = parentDim.content.width;
      } else {
        // Block-level auto width fills the containing block minus this box's
        // own margins, borders and padding (CSS 10.3.3)
        float used = style.marginLeft.value + style.marginRight.value +
                     style.borderLeftWidth.value + style.borderRightWidth.value +
                     style.paddingLeft.value + style.paddingRight.value;
        width = std::max(0.0f, parentDim.content.width - used);
      }
    } else if (style.width.unit == css::Length::Unit::Percent) {
      width = parentDim.content.width * (style.width.value / 100.0f);
    } else {
      width = style.width.value;
    }

    // Padding and border for box-sizing
    float paddingLeft = style.paddingLeft.value;
    float paddingRight = style.paddingRight.value;
    float borderLeft = style.borderLeftWidth.value;
    float borderRight = style.borderRightWidth.value;

    if (style.boxSizing == css::BoxSizing::BorderBox) {
      // border-box: width includes border and padding
      // content width = width - border - padding
      float contentWidth = width - borderLeft - paddingLeft - paddingRight - borderRight;
      box->dimensions().content.width = std::max(0.0f, contentWidth);
    } else {
      // content-box: width is content width
      box->dimensions().content.width = std::max(0.0f, width);
    }

    // Padding/Margin/Border (simplified copy)
    box->dimensions().padding.left = style.paddingLeft.value;
    box->dimensions().padding.right = style.paddingRight.value;
    box->dimensions().padding.top = style.paddingTop.value;
    box->dimensions().padding.bottom = style.paddingBottom.value;

    // Border width
    box->dimensions().border.left = style.borderLeftWidth.value;
    box->dimensions().border.right = style.borderRightWidth.value;
    box->dimensions().border.top = style.borderTopWidth.value;
    box->dimensions().border.bottom = style.borderBottomWidth.value;

    // Margin handling with auto support
    float marginLeft = style.marginLeft.value;
    float marginRight = style.marginRight.value;

    if (style.display == css::Display::Block &&
        style.width.unit != css::Length::Unit::Auto) {
      // Calculate available space
      float totalWidth = parentDim.content.width;
      float usedWidth = marginLeft + box->dimensions().border.left +
                        box->dimensions().padding.left +
                        box->dimensions().content.width +
                        box->dimensions().padding.right +
                        box->dimensions().border.right + marginRight;

      if (usedWidth < totalWidth) {
        if (style.marginLeft.unit == css::Length::Unit::Auto &&
            style.marginRight.unit == css::Length::Unit::Auto) {
          float remaining = totalWidth - usedWidth;
          marginLeft = remaining / 2.0f;
          marginRight = remaining / 2.0f;
        } else if (style.marginLeft.unit == css::Length::Unit::Auto) {
          marginLeft = totalWidth - usedWidth;
        } else if (style.marginRight.unit == css::Length::Unit::Auto) {
          marginRight = totalWidth - usedWidth;
        }
      }
    }

    box->dimensions().margin.left = marginLeft;
    box->dimensions().margin.right = marginRight;
    box->dimensions().margin.top = style.marginTop.value;
    box->dimensions().margin.bottom = style.marginBottom.value;
  }

  void calculateBlockHeight(LayoutBoxPtr box) {
    const auto &style = box->style();

    if (style.height.unit == css::Length::Unit::Auto) {
      float height = 0;
      if (isInlineFormattingContext(box)) {
        // Height = sum of line boxes
        for (const auto &line : box->lineBoxes()) {
          height += line.height();
        }
        // Also account for floats that extend below the line boxes
        for (const auto &f : box->leftFloats()) {
          float bottom = f.y + f.height -
                         box->dimensions().padding.top -
                         box->dimensions().border.top;
          if (bottom > height)
            height = bottom;
        }
        for (const auto &f : box->rightFloats()) {
          float bottom = f.y + f.height -
                         box->dimensions().padding.top -
                         box->dimensions().border.top;
          if (bottom > height)
            height = bottom;
        }
        box->dimensions().content.height = height;
      } else {
        // BFC: max of last normal-flow child bottom and float bottoms
        for (auto child : box->children()) {
          // Skip floated children — they're handled separately
          if (child->style().cssFloat != css::Float::None)
            continue;

          float childBottom = child->dimensions().content.y +
                              child->dimensions().content.height +
                              child->dimensions().padding.bottom +
                              child->dimensions().border.bottom;

          if (childBottom > height)
            height = childBottom;
        }

        // Check float overflow
        for (const auto &f : box->leftFloats()) {
          float bottom = f.y + f.height;
          if (bottom > height)
            height = bottom;
        }
        for (const auto &f : box->rightFloats()) {
          float bottom = f.y + f.height;
          if (bottom > height)
            height = bottom;
        }

        // Parent's content height = max(childBottom) - padding.top - border.top
        float contentHeight = height - box->dimensions().padding.top -
                              box->dimensions().border.top;
        if (contentHeight < 0)
          contentHeight = 0;
        box->dimensions().content.height = contentHeight;
      }
    } else {
      float paddingTop = style.paddingTop.value;
      float paddingBottom = style.paddingBottom.value;
      float borderTop = style.borderTopWidth.value;
      float borderBottom = style.borderBottomWidth.value;

      if (style.boxSizing == css::BoxSizing::BorderBox) {
        // border-box: height includes border and padding
        float contentHeight = style.height.value - borderTop - paddingTop - paddingBottom - borderBottom;
        box->dimensions().content.height = std::max(0.0f, contentHeight);
      } else {
        box->dimensions().content.height = style.height.value;
      }
    }
  }
};

// ---------------------------------------------------------------------------
// InlineFormattingContext implementation
// ---------------------------------------------------------------------------
// Defined here (after `LayoutAlgorithm` is complete) so the IFC can call
// `LayoutAlgorithm`'s private block-layout helpers via friendship without
// creating a circular include.

inline void InlineFormattingContext::layout(LayoutAlgorithm &algo,
                                            LayoutBoxPtr box) {
  if (!box)
    return;

  box->lineBoxes().clear();

  // Strut: minimum line height derived from the block container's own
  // font-size and line-height. Every line box will be at least this tall.
  float strutAscent = 0;
  float strutDescent = 0;
  computeStrut(box, strutAscent, strutDescent);

  // Initial line setup
  LineBox currentLine;
  currentLine.setY(box->dimensions().padding.top +
                   box->dimensions().border.top);
  currentLine.setStrut(strutAscent, strutDescent);

  // Floats reduce the available content width and shift the line start.
  float leftFloatWidth = 0;
  float rightFloatWidth = 0;
  computeFloatInsets(currentLine.y(), leftFloatWidth, rightFloatWidth);

  const float originalWidth = box->dimensions().content.width;
  float contentWidth = originalWidth - leftFloatWidth - rightFloatWidth;
  if (contentWidth < 0)
    contentWidth = 0;

  float startX = leftFloatWidth;
  float maxWidth = leftFloatWidth + contentWidth;

  // text-indent applies only to the first formatted line of each block
  // container (CSS §9.1). We model it by widening the start position of
  // the very first line; subsequent wrapped lines start at `startX`.
  float firstLineIndent = 0;
  if (box->style().textIndent.unit == css::Length::Unit::Px) {
    firstLineIndent = box->style().textIndent.value;
  } else if (box->style().textIndent.unit == css::Length::Unit::Percent) {
    firstLineIndent = contentWidth * (box->style().textIndent.value / 100.0f);
  }

  float currentX = startX + firstLineIndent;
  bool lineHasContent = false;

  std::vector<LayoutBoxPtr> inlineItems;
  collectInlineItems(box, inlineItems);

  for (auto &item : inlineItems) {
    if (item->type() == BoxType::InlineBlockNode) {
      layoutInlineBlock(algo, box, item, currentLine, currentX, contentWidth,
                        maxWidth, startX, lineHasContent);
    } else if (item->node() &&
               item->node()->nodeType() == dom::NodeType::Text) {
      layoutText(box, item, currentLine, currentX, contentWidth, maxWidth, startX,
                 lineHasContent);
    }
    // Other inline-level box types (e.g. raw InlineNode wrappers around
    // non-text content) are intentionally skipped in this MVP — they
    // contribute no fragments but their text descendants were already
    // collected by `collectInlineItems`.
  }

  if (!currentLine.fragments().empty()) {
    currentLine.finalizeAlignment();
    box->addLineBox(currentLine);
  }

  applyTextAlignment(box, contentWidth, firstLineIndent);

  // Set the block container's content height to the sum of line heights.
  float totalHeight = 0;
  for (const auto &line : box->lineBoxes()) {
    totalHeight += line.height();
  }
  box->dimensions().content.height = totalHeight;
}

inline void InlineFormattingContext::collectInlineItems(
    LayoutBoxPtr box, std::vector<LayoutBoxPtr> &out) const {
  if (!box)
    return;
  for (auto &child : box->children()) {
    if (!child)
      continue;
    // A text node is a leaf — collect it directly even if its box type is
    // InlineNode (which is the common case in our layout tree).
    if (child->node() &&
        child->node()->nodeType() == dom::NodeType::Text) {
      out.push_back(child);
      continue;
    }
    if (child->type() == BoxType::InlineNode ||
        child->type() == BoxType::AnonymousInline) {
      // Transparent inline wrapper around non-text content: descend so its
      // descendants participate in this IFC.
      collectInlineItems(child, out);
    } else if (child->type() == BoxType::InlineBlockNode) {
      out.push_back(child);
    }
    // Block-level children are not part of an IFC — they should have been
    // wrapped into anonymous blocks by the layout tree builder.
  }
}

inline void InlineFormattingContext::layoutText(
    LayoutBoxPtr parent, LayoutBoxPtr nodeBox, LineBox &currentLine,
    float &currentX, float &contentWidth, float &maxWidth, float &startX, bool &lineHasContent) {
  if (!nodeBox || !nodeBox->node())
    return;

  const std::string &text = nodeBox->node()->textContent();
  if (text.empty())
    return;

  const auto &style = nodeBox->style();
  const auto whiteSpace = style.whiteSpace;

  // Apply text-transform to get the display text for measurement.
  std::string displayText = text;
  if (style.textTransform == css::TextTransform::Uppercase) {
    std::transform(displayText.begin(), displayText.end(), displayText.begin(),
                   [](unsigned char c) { return std::toupper(c); });
  } else if (style.textTransform == css::TextTransform::Lowercase) {
    std::transform(displayText.begin(), displayText.end(), displayText.begin(),
                   [](unsigned char c) { return std::tolower(c); });
  } else if (style.textTransform == css::TextTransform::Capitalize) {
    bool nextUpper = true;
    for (auto &c : displayText) {
      if (std::isspace(static_cast<unsigned char>(c))) {
        nextUpper = true;
      } else if (nextUpper) {
        c = std::toupper(static_cast<unsigned char>(c));
        nextUpper = false;
      }
    }
  }

  // Resolve font metrics for this fragment's text.
  std::string fontFamily = style.fontFamily;
  int fontSize = static_cast<int>(style.fontSize.value);
  if (fontSize <= 0)
    fontSize = 16;

  auto &metrics = renderer::TextMetrics::instance();
  const float spaceWidth = metrics.spaceWidth(fontFamily, fontSize);
  const float fontLineHeight = effectiveLineHeight(style, fontSize);

  // The text baseline sits at fontAscender below the fragment's top.
  // We approximate using the measurement of a representative word so we
  // don't need a separate font metrics call for ascender/descender.
  renderer::TextMeasurement probe =
      metrics.measureWord("Mg", fontFamily, fontSize);
  float ascender = probe.ascender > 0 ? probe.ascender : fontLineHeight * 0.8f;
  float descender =
      probe.descender != 0 ? std::fabs(probe.descender) : fontLineHeight * 0.2f;
  // Fragment height is at least the effective line-height of this text.
  float fragHeight = std::max(fontLineHeight, ascender + descender);
  float fragBaseline = ascender;

  // Decide how to treat whitespace and wrapping.
  const bool preserveSpaces =
      (whiteSpace == css::WhiteSpace::Pre ||
       whiteSpace == css::WhiteSpace::PreWrap);
  const bool preserveNewlines =
      (whiteSpace == css::WhiteSpace::Pre ||
       whiteSpace == css::WhiteSpace::PreWrap ||
       whiteSpace == css::WhiteSpace::PreLine);
  const bool allowWrap =
      (whiteSpace == css::WhiteSpace::Normal ||
       whiteSpace == css::WhiteSpace::PreWrap ||
       whiteSpace == css::WhiteSpace::PreLine);

  auto emitWord = [&](size_t start, size_t end) {
    if (start >= end)
      return;
    const size_t len = end - start;
    const std::string word = displayText.substr(start, len);
    float wordWidth = metrics.measureWord(word, fontFamily, fontSize).width;

    // letter-spacing: add spacing between each character
    if (style.letterSpacing != 0.0f && len > 1) {
      wordWidth += style.letterSpacing * static_cast<float>(len - 1);
    }

    // Wrap if the word does not fit on the current line and we are not at
    // the line's start (a single word longer than the line is allowed to
    // overflow rather than be split — word breaking is not implemented).
    if (allowWrap && lineHasContent &&
        currentX + wordWidth > maxWidth + 0.001f) {
      breakLine(parent, currentLine, currentX, contentWidth, maxWidth, startX, lineHasContent);
    }

    BoxFragment frag;
    frag.box = nodeBox;
    frag.x = currentX - startX;
    frag.y = 0;
    frag.width = wordWidth;
    frag.height = fragHeight;
    frag.baseline = fragBaseline;
    frag.ascent = ascender;
    frag.descent = descender;
    frag.startOffset = start;
    frag.endOffset = end;
    frag.verticalAlign = style.verticalAlign;
    frag.verticalAlignOffset = style.verticalAlignOffset;
    frag.textDecorationLine = style.textDecorationLine;
    frag.isText = true;

    currentLine.addFragment(frag);
    currentX += wordWidth;
    lineHasContent = true;
  };

  auto emitSpace = [&]() {
    if (lineHasContent) {
      currentX += spaceWidth + style.wordSpacing;
    }
  };

  auto forceBreak = [&]() {
    breakLine(parent, currentLine, currentX, contentWidth, maxWidth, startX, lineHasContent);
  };

  size_t pos = 0;
  while (pos < displayText.length()) {
    char c = displayText[pos];

    if (c == '\n') {
      if (preserveNewlines) {
        forceBreak();
        ++pos;
        continue;
      }
      // For Normal/NoWrap, newlines are treated as a collapsible space.
      ++pos;
      while (pos < displayText.length() &&
             std::isspace(static_cast<unsigned char>(displayText[pos])) &&
             displayText[pos] != '\n') {
        ++pos;
      }
      emitSpace();
      continue;
    }

    if (std::isspace(static_cast<unsigned char>(c))) {
      if (preserveSpaces) {
        // Preserve each space character as its own (width) contribution.
        size_t runStart = pos;
        while (pos < displayText.length() &&
               std::isspace(static_cast<unsigned char>(displayText[pos])) &&
               displayText[pos] != '\n') {
          ++pos;
        }
        size_t runLen = pos - runStart;
        if (lineHasContent) {
          currentX += spaceWidth * static_cast<float>(runLen);
        }
      } else {
        // Collapse runs of whitespace to a single space.
        ++pos;
        while (pos < displayText.length() &&
               std::isspace(static_cast<unsigned char>(displayText[pos])) &&
               displayText[pos] != '\n') {
          ++pos;
        }
        emitSpace();
      }
      continue;
    }

    // Word: run of non-whitespace characters.
    size_t wordStart = pos;
    unsigned char uc = static_cast<unsigned char>(displayText[pos]);
    if (uc >= 0xE0) {
      // Basic UTF-8 CJK check (e.g. 3-byte characters, Chinese is typically in E0..EF)
      int seqLen = 3;
      if ((uc & 0xF0) == 0xE0) seqLen = 3;
      else if ((uc & 0xF8) == 0xF0) seqLen = 4;
      
      pos += seqLen;
      if (pos > displayText.length()) pos = displayText.length();
    } else {
      while (pos < displayText.length() &&
             !std::isspace(static_cast<unsigned char>(displayText[pos]))) {
        unsigned char currUc = static_cast<unsigned char>(displayText[pos]);
        if (currUc >= 0xE0) break; // Break word at CJK boundary
        ++pos;
      }
    }
    emitWord(wordStart, pos);
  }
}

inline void InlineFormattingContext::layoutInlineBlock(
    LayoutAlgorithm &algo, LayoutBoxPtr parent, LayoutBoxPtr item,
    LineBox &currentLine, float &currentX, float &contentWidth,
    float &maxWidth, float &startX, bool &lineHasContent) {
  if (!item)
    return;

  // Atomic inline-level boxes are laid out as if they were block
  // containers: their width/height is computed first, then they are
  // measured as a single fragment for inline flow purposes.
  Dimensions parentDim;
  parentDim.content.width = contentWidth;

  // Friend access into LayoutAlgorithm's private block-layout helpers.
  algo.calculateBlockWidth(item, parentDim);
  algo.layoutBlock(item);
  algo.calculateBlockHeight(item);

  const auto &d = item->dimensions();
  const float itemWidth = d.margin.left + d.border.left + d.padding.left +
                          d.content.width + d.padding.right +
                          d.border.right + d.margin.right;
  const float itemHeight = d.margin.top + d.border.top + d.padding.top +
                           d.content.height + d.padding.bottom +
                           d.border.bottom + d.margin.bottom;

  // Wrap to a new line if the inline-block doesn't fit.
  if (lineHasContent && currentX + itemWidth > maxWidth + 0.001f && currentX > startX) {
    breakLine(parent, currentLine, currentX, contentWidth, maxWidth, startX, lineHasContent);
  }

  // Per CSS 2.1 §10.8.1, the baseline of an `inline-block` whose
  // `overflow` is `visible` is its bottom margin edge. We approximate by
  // using the bottom of the border box (margins don't affect the baseline).
  const float fragWidth = itemWidth - d.margin.left - d.margin.right;
  const float fragHeight = itemHeight - d.margin.top - d.margin.bottom;
  const float fragBaseline = fragHeight; // bottom-edge baseline

  BoxFragment frag;
  frag.box = item;
  frag.x = currentX + d.margin.left - startX;
  frag.y = 0;
  frag.width = fragWidth;
  frag.height = fragHeight;
  frag.baseline = fragBaseline;
  frag.ascent = fragBaseline;
  frag.descent = 0;
  frag.verticalAlign = item->style().verticalAlign;
  frag.verticalAlignOffset = item->style().verticalAlignOffset;
  frag.isText = false;

  currentLine.addFragment(frag);
  currentX += itemWidth;
  lineHasContent = true;
}

inline void InlineFormattingContext::applyTextAlignment(
    LayoutBoxPtr box, float contentWidth, float textIndent) const {
  const auto &style = box->style();
  if (style.textAlign == css::TextAlign::Left)
    return;

  bool firstLine = true;
  bool isLastLine = false;
  size_t lineCount = box->lineBoxes().size();
  size_t lineIndex = 0;

  for (auto &lineBox : box->lineBoxes()) {
    ++lineIndex;
    isLastLine = (lineIndex == lineCount);

    float usedWidth = 0;
    for (const auto &frag : lineBox.fragments()) {
      usedWidth = std::max(usedWidth, frag.x + frag.width);
    }
    // The first line's `text-indent` shifts content right; account for it
    // when computing remaining space so centering is correct.
    float effectiveIndent = firstLine ? textIndent : 0;
    float available = contentWidth - effectiveIndent;

    float offset = 0;
    if (style.textAlign == css::TextAlign::Center) {
      offset = (available - usedWidth) / 2.0f;
    } else if (style.textAlign == css::TextAlign::Right) {
      offset = available - usedWidth;
    } else if (style.textAlign == css::TextAlign::Justify) {
      // Justify: distribute extra space between words on non-last lines
      if (!isLastLine) {
        size_t wordCount = 0;
        for (const auto &frag : lineBox.fragments()) {
          if (frag.isText)
            wordCount++;
        }
        if (wordCount > 1) {
          float extraSpace = available - usedWidth;
          float spacePerWord = extraSpace / static_cast<float>(wordCount - 1);
          float shift = 0;
          for (auto &frag : lineBox.fragments()) {
            frag.x += shift;
            if (frag.isText)
              shift += spacePerWord;
          }
        }
      }
      // Last line is left-aligned (standard behavior)
    }
    if (offset > 0)
      lineBox.shiftFragmentsX(offset);
    firstLine = false;
  }
}

inline void InlineFormattingContext::computeStrut(
    LayoutBoxPtr box, float &outAscent, float &outDescent) const {
  if (!box) {
    outAscent = 0;
    outDescent = 0;
    return;
  }

  const auto &style = box->style();
  int fontSize = static_cast<int>(style.fontSize.value);
  if (fontSize <= 0)
    fontSize = 16;

  const float lh = effectiveLineHeight(style, fontSize);
  auto &metrics = renderer::TextMetrics::instance();
  const float fontLineHeight =
      metrics.lineHeight(style.fontFamily, fontSize);
  // Approximate strut ascent/descent using font metrics plus half-leading.
  const float leading = std::max(0.0f, lh - fontLineHeight) / 2.0f;
  outAscent = (fontLineHeight * 0.8f) + leading;
  outDescent = (fontLineHeight * 0.2f) + leading;
}

inline float InlineFormattingContext::effectiveLineHeight(
    const css::ComputedStyle &style, int fontSize) const {
  const auto &lh = style.lineHeight;
  switch (lh.mode) {
    case css::LineHeight::Mode::Normal:
      return static_cast<float>(fontSize) * 1.2f;
    case css::LineHeight::Mode::Number:
      return static_cast<float>(fontSize) * lh.value;
    case css::LineHeight::Mode::Length:
      return lh.value;
    case css::LineHeight::Mode::Percent:
      return static_cast<float>(fontSize) * (lh.value / 100.0f);
  }
  return static_cast<float>(fontSize) * 1.2f;
}

inline void InlineFormattingContext::computeFloatInsets(
    float y, float &leftFloatWidth, float &rightFloatWidth) const {
  leftFloatWidth = 0;
  rightFloatWidth = 0;

  // Prefer the new FloatInfo lists if available
  if (!leftFloats_.empty() || !rightFloats_.empty()) {
    for (const auto &fi : leftFloats_) {
      if (y >= fi.y && y < fi.y + fi.height) {
        leftFloatWidth = std::max(leftFloatWidth, fi.width);
      }
    }
    for (const auto &fi : rightFloats_) {
      if (y >= fi.y && y < fi.y + fi.height) {
        rightFloatWidth = std::max(rightFloatWidth, fi.width);
      }
    }
    return;
  }

  // Legacy fallback: read from activeFloats_ LayoutBoxPtr list
  for (const auto &floatBox : activeFloats_) {
    if (!floatBox)
      continue;
    const auto &d = floatBox->dimensions();
    const float outerWidth = d.margin.left + d.border.left + d.padding.left +
                             d.content.width + d.padding.right +
                             d.border.right + d.margin.right;
    const float floatY = d.content.y - d.padding.top - d.border.top - d.margin.top;
    const float floatHeight = d.margin.top + d.border.top + d.padding.top +
                              d.content.height + d.padding.bottom +
                              d.border.bottom + d.margin.bottom;
    if (y >= floatY && y < floatY + floatHeight) {
      if (floatBox->style().cssFloat == css::Float::Left) {
        leftFloatWidth = std::max(leftFloatWidth, outerWidth);
      } else if (floatBox->style().cssFloat == css::Float::Right) {
        rightFloatWidth = std::max(rightFloatWidth, outerWidth);
      }
    }
  }
}

inline void InlineFormattingContext::breakLine(LayoutBoxPtr box,
                                               LineBox &currentLine,
                                               float &currentX,
                                               float &contentWidth,
                                               float &maxWidth,
                                               float &startX,
                                               bool &lineHasContent) const {
  if (currentLine.fragments().empty())
    return;

  currentLine.finalizeAlignment();
  box->addLineBox(currentLine);

  float nextY = currentLine.y() + currentLine.height();
  
  float strutAscent = 0;
  float strutDescent = 0;
  computeStrut(box, strutAscent, strutDescent);

  currentLine.clear();
  currentLine.setY(nextY);
  currentLine.setStrut(strutAscent, strutDescent);

  float leftFloatWidth = 0;
  float rightFloatWidth = 0;
  computeFloatInsets(nextY, leftFloatWidth, rightFloatWidth);

  const float originalWidth = box->dimensions().content.width;
  contentWidth = originalWidth - leftFloatWidth - rightFloatWidth;
  if (contentWidth < 0)
    contentWidth = 0;

  startX = leftFloatWidth;
  maxWidth = leftFloatWidth + contentWidth;

  currentX = startX;
  lineHasContent = false;
}

} // namespace layout
} // namespace xiaopeng
