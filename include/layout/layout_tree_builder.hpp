#pragma once

#include "../css/computed_style.hpp"
#include "../css/style_resolver.hpp"
#include "../dom/dom.hpp"
#include "layout_box.hpp"

namespace xiaopeng {
namespace layout {

class LayoutTreeBuilder {
public:
  LayoutTreeBuilder(const css::StyleSheet &sheet) : sheet_(sheet) {}

  LayoutBoxPtr build(dom::NodePtr node, const css::ComputedStyle *parentStyle = nullptr) {
    if (!node)
      return nullptr;

    LayoutBoxPtr layoutNode = nullptr;

    // 1. Determine display type
    css::Display display = css::Display::Inline; // Default
    css::ComputedStyle style;

    if (node->nodeType() == dom::NodeType::Element) {
      if (auto element = std::dynamic_pointer_cast<dom::Element>(node)) {
        style = resolver_.resolveStyle(element, sheet_, parentStyle);
        display = style.display;

        // <img> intrinsic sizing: read HTML width/height attributes
        if (element->localName() == "img") {
          display = css::Display::InlineBlock; // Replaced elements act as inline-blocks inside IFC

          // Read width attribute
          auto widthAttr = element->getAttribute("width");
          if (widthAttr.has_value() && !widthAttr.value().empty() &&
              style.width.unit == css::Length::Unit::Auto) {
            try {
              float w = std::stof(widthAttr.value());
              style.width = css::Length::Px(w);
            } catch (...) {
            }
          }

          // Read height attribute
          auto heightAttr = element->getAttribute("height");
          if (heightAttr.has_value() && !heightAttr.value().empty() &&
              style.height.unit == css::Length::Unit::Auto) {
            try {
              float h = std::stof(heightAttr.value());
              style.height = css::Length::Px(h);
            } catch (...) {
            }
          }

          // Default replaced element size (HTML spec: 300x150)
          if (style.width.unit == css::Length::Unit::Auto) {
            style.width = css::Length::Px(300);
          }
          if (style.height.unit == css::Length::Unit::Auto) {
            style.height = css::Length::Px(150);
          }
        }
      }
    } else if (node->nodeType() == dom::NodeType::Text) {
      // Text nodes inherit display context, treating as inline for now
      display = css::Display::Inline;
      if (parentStyle) {
        style = *parentStyle;
      }
    }

    // 2. Skip if display:none
    if (display == css::Display::None) {
      return nullptr;
    }

    // 3. Create Box
    if (display == css::Display::Block) {
      layoutNode = std::make_shared<LayoutBox>(BoxType::BlockNode, style, node);
    } else if (display == css::Display::InlineBlock) {
      layoutNode = std::make_shared<LayoutBox>(BoxType::InlineBlockNode, style, node);
    } else {
      // For simplicity, treat everything else as inline for now.
      // Text nodes are always inline.
      layoutNode =
          std::make_shared<LayoutBox>(BoxType::InlineNode, style, node);
    }

    // 4. Recurse for children
    for (auto child : node->childNodes()) {
      auto childLayout = build(child, &style);
      if (childLayout) {
        // Anonymous box generation logic (simplified)
        if (layoutNode->type() == BoxType::BlockNode &&
            childLayout->type() == BoxType::InlineNode) {
          // Check if we already have an anonymous block wrapper at the end?
          // Actually, if a block box has inline children, it establishes an
          // IFC. But if it has mixed inline and block children, inlines must be
          // wrapped in anonymous blocks.

          // Simplified: just add it for now. Layout algorithm will handle
          // IFC/BFC mismatch? Or we wrap here. Let's wrap here to ensure
          // invariant: Block container has either all Block children or all
          // Inline children.

          auto lastChild = layoutNode->getLastChild();
          if (lastChild && lastChild->type() == BoxType::AnonymousBlock) {
            lastChild->addChild(childLayout);
          } else {
            // Create new anonymous block
            // Wait, standard says:
            // If a block container box (layoutNode) has a block-level box
            // inside it, then we must force all inline content into anonymous
            // block boxes.

            // We don't verify if there ARE block children yet (recursion
            // order). Actually we are iterating children. If we encounter a
            // Block child later, we might need to fix up previous inlines?

            // To keep it simple for MVP:
            // Just append. The Layout Algorithm for Block Flow will handle
            // inline children by creating line boxes.
            layoutNode->addChild(childLayout);
          }
        } else {
          layoutNode->addChild(childLayout);
        }
      }
    }

    return layoutNode;
  }

private:
  const css::StyleSheet &sheet_;
  css::StyleResolver resolver_;
};

} // namespace layout
} // namespace xiaopeng
