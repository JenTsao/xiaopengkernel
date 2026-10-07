// test_css_complete.cpp — CSS completion tests: inheritance, CSS-wide
// keywords, shorthand expansion, em/rem units, :is/:where/:has, var() fallback
//
// Pure C++ (no QuickJS). Uses StyleResolver directly.

#include "test_framework.hpp"

#include "../include/css/css_parser.hpp"
#include "../include/css/style_resolver.hpp"
#include "../include/dom/dom.hpp"

#include <memory>
#include <string>

using namespace xiaopeng;
using namespace xiaopeng::css;
using namespace xiaopeng::dom;

namespace {

StyleSheet parseSheet(const std::string &css) {
  CssParser parser(css);
  return parser.parse();
}

// html > body > div#box > p#target.text
struct Tree {
  std::shared_ptr<Document> doc;
  ElementPtr html, body, div, p;

  Tree() {
    doc = std::make_shared<Document>();
    html = doc->createElement("html");
    body = doc->createElement("body");
    div = doc->createElement("div");
    p = doc->createElement("p");
    div->setId("box");
    p->setId("target");
    p->setClassName("text");
    div->appendChild(p);
    body->appendChild(div);
    html->appendChild(body);
    doc->appendChild(html);
  }
};

} // namespace

// ── Inheritance ──────────────────────────────────────────────

TEST(Inheritance_ColorAndFont) {
  Tree t;
  auto sheet = parseSheet("body { color: red; font-family: custom-font; }");
  StyleResolver resolver;
  auto bodyStyle = resolver.resolveStyle(t.body, sheet, nullptr);
  auto divStyle = resolver.resolveStyle(t.div, sheet, &bodyStyle);
  auto pStyle = resolver.resolveStyle(t.p, sheet, &divStyle);

  EXPECT_TRUE(divStyle.color.r == 255);
  EXPECT_TRUE(pStyle.color.r == 255);
  EXPECT_STREQ(pStyle.fontFamily.c_str(), "custom-font");
}

TEST(Inheritance_ExplicitDeclarationOverrides) {
  Tree t;
  auto sheet = parseSheet("body { color: red; } p { color: blue; }");
  StyleResolver resolver;
  auto bodyStyle = resolver.resolveStyle(t.body, sheet, nullptr);
  auto divStyle = resolver.resolveStyle(t.div, sheet, &bodyStyle);
  auto pStyle = resolver.resolveStyle(t.p, sheet, &divStyle);

  EXPECT_TRUE(pStyle.color.b == 255);
  EXPECT_TRUE(pStyle.color.r == 0);
  // div has no own color → inherits red
  EXPECT_TRUE(divStyle.color.r == 255);
}

TEST(Inheritance_CustomProperties) {
  Tree t;
  auto sheet = parseSheet("body { --brand: #0000ff; } p { color: var(--brand); }");
  StyleResolver resolver;
  auto bodyStyle = resolver.resolveStyle(t.body, sheet, nullptr);
  auto divStyle = resolver.resolveStyle(t.div, sheet, &bodyStyle);
  auto pStyle = resolver.resolveStyle(t.p, sheet, &divStyle);

  EXPECT_TRUE(pStyle.color.b == 255);
  EXPECT_TRUE(pStyle.color.r == 0);
}

TEST(Inheritance_TextAlign) {
  Tree t;
  auto sheet = parseSheet("body { text-align: center; }");
  StyleResolver resolver;
  auto bodyStyle = resolver.resolveStyle(t.body, sheet, nullptr);
  auto pStyle = resolver.resolveStyle(t.p, sheet, &bodyStyle);
  EXPECT_TRUE(pStyle.textAlign == TextAlign::Center);
}

// ── CSS-wide keywords ────────────────────────────────────────

TEST(InheritKeyword) {
  Tree t;
  auto sheet = parseSheet(
      "body { margin: 10px 20px; text-align: center; } "
      "p { margin: inherit; text-align: inherit; }");
  StyleResolver resolver;
  auto bodyStyle = resolver.resolveStyle(t.body, sheet, nullptr);
  auto pStyle = resolver.resolveStyle(t.p, sheet, &bodyStyle);

  EXPECT_TRUE(pStyle.marginTop.value == 10.0f);
  EXPECT_TRUE(pStyle.marginRight.value == 20.0f);
  EXPECT_TRUE(pStyle.textAlign == TextAlign::Center);
}

TEST(InitialKeyword) {
  Tree t;
  auto sheet = parseSheet("body { color: red; margin-top: 5px; } "
                          "p { color: initial; margin-top: initial; }");
  StyleResolver resolver;
  auto bodyStyle = resolver.resolveStyle(t.body, sheet, nullptr);
  auto pStyle = resolver.resolveStyle(t.p, sheet, &bodyStyle);

  // back to defaults, not the parent values
  EXPECT_TRUE(pStyle.color.r == 0);
  EXPECT_TRUE(pStyle.marginTop.value == 0.0f);
}

TEST(UnsetKeyword) {
  Tree t;
  auto sheet = parseSheet("body { color: red; } p { color: unset; margin: unset; }");
  StyleResolver resolver;
  auto bodyStyle = resolver.resolveStyle(t.body, sheet, nullptr);
  auto pStyle = resolver.resolveStyle(t.p, sheet, &bodyStyle);

  // inherited property → inherits
  EXPECT_TRUE(pStyle.color.r == 255);
  // non-inherited property → initial
  EXPECT_TRUE(pStyle.marginTop.value == 0.0f);
}

TEST(InheritKeyword_LonghandBox) {
  Tree t;
  auto sheet = parseSheet(
      "body { margin-top: 5px; padding-left: 7px; border-top-width: 2px; "
      "overflow-x: hidden; } "
      "p { margin-top: inherit; padding-left: inherit; "
      "border-top-width: inherit; overflow-x: inherit; }");
  StyleResolver resolver;
  auto bodyStyle = resolver.resolveStyle(t.body, sheet, nullptr);
  auto pStyle = resolver.resolveStyle(t.p, sheet, &bodyStyle);

  // longhands must be copyable too, not just the shorthands
  EXPECT_TRUE(pStyle.marginTop.value == 5.0f);
  EXPECT_TRUE(pStyle.paddingLeft.value == 7.0f);
  EXPECT_TRUE(pStyle.borderTopWidth.value == 2.0f);
  EXPECT_TRUE(pStyle.overflowX == Overflow::Hidden);
}

// ── Shorthand expansion ──────────────────────────────────────

TEST(MarginShorthand_MultiValue) {
  Tree t;
  {
    auto sheet = parseSheet("p { margin: 10px 20px; }");
    StyleResolver resolver;
    auto style = resolver.resolveStyle(t.p, sheet, nullptr);
    EXPECT_TRUE(style.marginTop.value == 10.0f);
    EXPECT_TRUE(style.marginBottom.value == 10.0f);
    EXPECT_TRUE(style.marginRight.value == 20.0f);
    EXPECT_TRUE(style.marginLeft.value == 20.0f);
  }
  {
    auto sheet = parseSheet("p { margin: 1px 2px 3px; }");
    StyleResolver resolver;
    auto style = resolver.resolveStyle(t.p, sheet, nullptr);
    EXPECT_TRUE(style.marginTop.value == 1.0f);
    EXPECT_TRUE(style.marginRight.value == 2.0f);
    EXPECT_TRUE(style.marginLeft.value == 2.0f);
    EXPECT_TRUE(style.marginBottom.value == 3.0f);
  }
  {
    auto sheet = parseSheet("p { margin: 1px 2px 3px 4px; }");
    StyleResolver resolver;
    auto style = resolver.resolveStyle(t.p, sheet, nullptr);
    EXPECT_TRUE(style.marginTop.value == 1.0f);
    EXPECT_TRUE(style.marginRight.value == 2.0f);
    EXPECT_TRUE(style.marginBottom.value == 3.0f);
    EXPECT_TRUE(style.marginLeft.value == 4.0f);
  }
}

TEST(PaddingShorthand_MultiValue) {
  Tree t;
  auto sheet = parseSheet("p { padding: 5px 15px 25px 35px; }");
  StyleResolver resolver;
  auto style = resolver.resolveStyle(t.p, sheet, nullptr);
  EXPECT_TRUE(style.paddingTop.value == 5.0f);
  EXPECT_TRUE(style.paddingRight.value == 15.0f);
  EXPECT_TRUE(style.paddingBottom.value == 25.0f);
  EXPECT_TRUE(style.paddingLeft.value == 35.0f);
}

TEST(BorderShorthands_MultiValue) {
  Tree t;
  auto sheet = parseSheet("p { border-width: 1px 2px; border-color: red blue; }");
  StyleResolver resolver;
  auto style = resolver.resolveStyle(t.p, sheet, nullptr);
  EXPECT_TRUE(style.borderTopWidth.value == 1.0f);
  EXPECT_TRUE(style.borderBottomWidth.value == 1.0f);
  EXPECT_TRUE(style.borderRightWidth.value == 2.0f);
  EXPECT_TRUE(style.borderLeftWidth.value == 2.0f);
  EXPECT_TRUE(style.borderTopColor.r == 255);
  EXPECT_TRUE(style.borderLeftColor.b == 255);
}

TEST(OverflowShorthand) {
  Tree t;
  {
    auto sheet = parseSheet("p { overflow: hidden auto; }");
    StyleResolver resolver;
    auto style = resolver.resolveStyle(t.p, sheet, nullptr);
    EXPECT_TRUE(style.overflowX == Overflow::Hidden);
    EXPECT_TRUE(style.overflowY == Overflow::Auto);
  }
  {
    auto sheet = parseSheet("p { overflow: scroll; }");
    StyleResolver resolver;
    auto style = resolver.resolveStyle(t.p, sheet, nullptr);
    EXPECT_TRUE(style.overflowX == Overflow::Scroll);
    EXPECT_TRUE(style.overflowY == Overflow::Scroll);
  }
}

TEST(FlexShorthand) {
  Tree t;
  {
    auto sheet = parseSheet("p { flex: 1; }");
    StyleResolver resolver;
    auto style = resolver.resolveStyle(t.p, sheet, nullptr);
    EXPECT_TRUE(style.flexGrow == 1.0f);
    EXPECT_TRUE(style.flexShrink == 1.0f);
    EXPECT_TRUE(style.flexBasis.unit == Length::Unit::Percent);
  }
  {
    auto sheet = parseSheet("p { flex: auto; }");
    StyleResolver resolver;
    auto style = resolver.resolveStyle(t.p, sheet, nullptr);
    EXPECT_TRUE(style.flexGrow == 1.0f);
    EXPECT_TRUE(style.flexBasis.unit == Length::Unit::Auto);
  }
  {
    auto sheet = parseSheet("p { flex: none; }");
    StyleResolver resolver;
    auto style = resolver.resolveStyle(t.p, sheet, nullptr);
    EXPECT_TRUE(style.flexGrow == 0.0f);
    EXPECT_TRUE(style.flexShrink == 0.0f);
  }
  {
    auto sheet = parseSheet("p { flex: 2 3 50px; }");
    StyleResolver resolver;
    auto style = resolver.resolveStyle(t.p, sheet, nullptr);
    EXPECT_TRUE(style.flexGrow == 2.0f);
    EXPECT_TRUE(style.flexShrink == 3.0f);
    EXPECT_TRUE(style.flexBasis.unit == Length::Unit::Px);
    EXPECT_TRUE(style.flexBasis.value == 50.0f);
  }
}

TEST(BackgroundShorthand_Color) {
  Tree t;
  auto sheet = parseSheet("p { background: #ff0000; } div { background: url(bg.png); }");
  StyleResolver resolver;
  auto pStyle = resolver.resolveStyle(t.p, sheet, nullptr);
  auto divStyle = resolver.resolveStyle(t.div, sheet, nullptr);

  EXPECT_TRUE(pStyle.backgroundColor.r == 255);
  // non-color value must not become a color
  EXPECT_TRUE(divStyle.backgroundColor.a == 0);
  EXPECT_STREQ(divStyle.otherProperties.at("background").c_str(), "url(bg.png)");
}

// ── Relative units ───────────────────────────────────────────

TEST(FontSizeEmAndPercent_RelativeToParent) {
  Tree t;
  {
    auto sheet = parseSheet("body { font-size: 20px; } p { font-size: 1.5em; }");
    StyleResolver resolver;
    auto bodyStyle = resolver.resolveStyle(t.body, sheet, nullptr);
    auto pStyle = resolver.resolveStyle(t.p, sheet, &bodyStyle);
    EXPECT_TRUE(pStyle.fontSize.unit == Length::Unit::Px);
    EXPECT_TRUE(pStyle.fontSize.value == 30.0f);
  }
  {
    auto sheet = parseSheet("body { font-size: 20px; } p { font-size: 150%; }");
    StyleResolver resolver;
    auto bodyStyle = resolver.resolveStyle(t.body, sheet, nullptr);
    auto pStyle = resolver.resolveStyle(t.p, sheet, &bodyStyle);
    EXPECT_TRUE(pStyle.fontSize.value == 30.0f);
  }
}

TEST(EmUnits_ResolveAgainstFontSize) {
  Tree t;
  auto sheet = parseSheet("p { font-size: 20px; margin: 1em; padding: 0.5em; }");
  StyleResolver resolver;
  auto style = resolver.resolveStyle(t.p, sheet, nullptr);
  EXPECT_TRUE(style.marginTop.value == 20.0f);
  EXPECT_TRUE(style.paddingTop.value == 10.0f);
  EXPECT_TRUE(style.marginTop.unit == Length::Unit::Px);
}

TEST(RemUnits_ResolveAgainstRootFontSize) {
  Tree t;
  {
    auto sheet = parseSheet("p { margin: 2rem; font-size: 2rem; }");
    StyleResolver resolver;
    auto style = resolver.resolveStyle(t.p, sheet, nullptr);
    EXPECT_TRUE(style.marginTop.value == 32.0f);
    EXPECT_TRUE(style.fontSize.value == 32.0f);
  }
  {
    auto sheet = parseSheet("p { margin: 2rem; }");
    StyleResolver resolver;
    resolver.setRootFontSize(20.0f);
    auto style = resolver.resolveStyle(t.p, sheet, nullptr);
    EXPECT_TRUE(style.marginTop.value == 40.0f);
  }
}

// ── :is() / :where() / :has() ────────────────────────────────

struct PseudoTree {
  std::shared_ptr<Document> doc;
  ElementPtr body, div, pa, pb, span;

  PseudoTree() {
    doc = std::make_shared<Document>();
    body = doc->createElement("body");
    div = doc->createElement("div");
    pa = doc->createElement("p");
    pa->setClassName("a");
    pb = doc->createElement("p");
    pb->setClassName("b");
    span = doc->createElement("span");
    div->appendChild(pa);
    div->appendChild(pb);
    div->appendChild(span);
    body->appendChild(div);
    doc->appendChild(body);
  }
};

TEST(IsPseudo_MatchesListAndComplex) {
  PseudoTree t;
  {
    auto sheet = parseSheet("p:is(.a, .b) { color: red; }");
    StyleResolver resolver;
    auto paStyle = resolver.resolveStyle(t.pa, sheet, nullptr);
    auto pbStyle = resolver.resolveStyle(t.pb, sheet, nullptr);
    auto spanStyle = resolver.resolveStyle(t.span, sheet, nullptr);
    EXPECT_TRUE(paStyle.color.r == 255);
    EXPECT_TRUE(pbStyle.color.r == 255);
    EXPECT_TRUE(spanStyle.color.r == 0);
  }
  {
    // complex selector inside :is()
    auto sheet = parseSheet("span:is(div > span) { color: blue; }");
    StyleResolver resolver;
    auto spanStyle = resolver.resolveStyle(t.span, sheet, nullptr);
    EXPECT_TRUE(spanStyle.color.b == 255);
  }
}

TEST(WherePseudo_Matches) {
  PseudoTree t;
  auto sheet = parseSheet("p:where(.a) { color: red; }");
  StyleResolver resolver;
  auto paStyle = resolver.resolveStyle(t.pa, sheet, nullptr);
  auto pbStyle = resolver.resolveStyle(t.pb, sheet, nullptr);
  EXPECT_TRUE(paStyle.color.r == 255);
  EXPECT_TRUE(pbStyle.color.r == 0);
}

TEST(FunctionalPseudoSpecificity) {
  // :where() is always zero, regardless of its arguments (CSS Selectors 4)
  auto where = CssParser::parseSelectorList(":where(#a, .b)");
  EXPECT_TRUE(where.size() == 1);
  Specificity ws = where[0].specificity();
  EXPECT_TRUE(ws.a == 0 && ws.b == 0 && ws.c == 0);

  // :is() weighs its most specific argument
  auto is = CssParser::parseSelectorList(":is(#a, .b)");
  EXPECT_TRUE(is.size() == 1);
  Specificity isSpec = is[0].specificity();
  EXPECT_TRUE(isSpec.a == 1 && isSpec.b == 0 && isSpec.c == 0);

  // argument weight, not a blanket (0,1,0)
  auto isLight = CssParser::parseSelectorList(":is(em, span)");
  Specificity isLightSpec = isLight[0].specificity();
  EXPECT_TRUE(isLightSpec.a == 0 && isLightSpec.b == 0 && isLightSpec.c == 1);

  // :has() weighs its relative argument
  auto has = CssParser::parseSelectorList("div:has(> em)");
  EXPECT_TRUE(has.size() == 1);
  Specificity hasSpec = has[0].specificity();
  EXPECT_TRUE(hasSpec.a == 0 && hasSpec.b == 0 && hasSpec.c == 2);

  // plain pseudo-classes stay (0,1,0)
  auto hover = CssParser::parseSelectorList("a:hover");
  Specificity hoverSpec = hover[0].specificity();
  EXPECT_TRUE(hoverSpec.a == 0 && hoverSpec.b == 1 && hoverSpec.c == 1);
}

TEST(WhereSpecificity_ZeroWeight) {
  PseudoTree t;
  // p:where(.a) weighs (0,0,1) — ties with p and loses to source order.
  // A (0,1,1) weight would wrongly let the red rule win.
  auto sheet = parseSheet("p:where(.a) { color: red; } p { color: green; }");
  StyleResolver resolver;
  auto paStyle = resolver.resolveStyle(t.pa, sheet, nullptr);
  EXPECT_TRUE(paStyle.color.g == 255);
  EXPECT_TRUE(paStyle.color.r == 0);
}

TEST(IsSpecificity_MostSpecificArgument) {
  auto doc = std::make_shared<Document>();
  auto p = doc->createElement("p");
  p->setId("target");
  p->setClassName("a");
  doc->appendChild(p);

  // :is() weighs its most specific argument even when unmatched:
  // p:is(.a, #nope) = (1,0,1) beats #target (1,0,0). A plain (0,1,1)
  // pseudo-class weight would lose to the ID rule.
  auto sheet = parseSheet("p:is(.a, #nope) { color: red; } #target { color: green; }");
  StyleResolver resolver;
  auto style = resolver.resolveStyle(p, sheet, nullptr);
  EXPECT_TRUE(style.color.r == 255);
  EXPECT_TRUE(style.color.g == 0);
}

TEST(HasSpecificity_ArgumentWeight) {
  auto doc = std::make_shared<Document>();
  auto div = doc->createElement("div");
  div->setClassName("x");
  auto em = doc->createElement("em");
  div->appendChild(em);
  doc->appendChild(div);

  // :has() weighs its argument: div:has(em) = (0,0,2) loses to .x (0,1,0).
  // A plain (0,1,1) weight would win by source order.
  auto sheet = parseSheet(".x { color: green; } div:has(em) { color: red; }");
  StyleResolver resolver;
  auto style = resolver.resolveStyle(div, sheet, nullptr);
  EXPECT_TRUE(style.color.g == 255);
  EXPECT_TRUE(style.color.r == 0);
}

TEST(HasPseudo_Relations) {
  auto doc = std::make_shared<Document>();
  auto body = doc->createElement("body");
  auto div = doc->createElement("div");
  auto em = doc->createElement("em");
  auto span = doc->createElement("span");
  div->appendChild(em);
  div->appendChild(span);
  auto ul = doc->createElement("ul");
  auto li1 = doc->createElement("li");
  auto li2 = doc->createElement("li");
  auto li3 = doc->createElement("li");
  ul->appendChild(li1);
  ul->appendChild(li2);
  ul->appendChild(li3);
  body->appendChild(div);
  body->appendChild(ul);
  doc->appendChild(body);

  StyleResolver resolver;

  {
    // descendant relation
    auto sheet = parseSheet("div:has(span) { color: red; }");
    auto style = resolver.resolveStyle(div, sheet, nullptr);
    EXPECT_TRUE(style.color.r == 255);
  }
  {
    // child relation: em is a child, span matches deeper? both children here
    auto sheet = parseSheet("div:has(> em) { color: blue; }");
    auto style = resolver.resolveStyle(div, sheet, nullptr);
    EXPECT_TRUE(style.color.b == 255);
  }
  {
    auto sheet = parseSheet("div:has(> b) { color: blue; }");
    auto style = resolver.resolveStyle(div, sheet, nullptr);
    EXPECT_TRUE(style.color.b == 0);
  }
  {
    // next-sibling relation: li1 and li2 have a next li sibling
    auto sheet = parseSheet("li:has(+ li) { color: red; }");
    auto s1 = resolver.resolveStyle(li1, sheet, nullptr);
    auto s2 = resolver.resolveStyle(li2, sheet, nullptr);
    auto s3 = resolver.resolveStyle(li3, sheet, nullptr);
    EXPECT_TRUE(s1.color.r == 255);
    EXPECT_TRUE(s2.color.r == 255);
    EXPECT_TRUE(s3.color.r == 0);
  }
  {
    // subsequent-sibling relation
    auto sheet = parseSheet("li:has(~ li) { color: purple; }");
    auto s1 = resolver.resolveStyle(li1, sheet, nullptr);
    auto s3 = resolver.resolveStyle(li3, sheet, nullptr);
    EXPECT_TRUE(s1.color.r == 128);
    EXPECT_TRUE(s3.color.r == 0);
  }
}

// ── var() fallback ───────────────────────────────────────────

TEST(VarFallback) {
  Tree t;
  auto sheet = parseSheet("p { color: var(--missing, #00ff00); }");
  StyleResolver resolver;
  auto style = resolver.resolveStyle(t.p, sheet, nullptr);
  EXPECT_TRUE(style.color.g == 255);
  EXPECT_TRUE(style.color.r == 0);
}

int main() {
  return xiaopeng::test::runTests();
}
