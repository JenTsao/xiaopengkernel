// test_dom_complete.cpp — WHATWG DOM completion tests (C++ core, no JS)
//
// Covers: real selector engine (querySelector/querySelectorAll/matches/
// closest), ParentNode/ChildNode mixins, CharacterData API, Text.splitText,
// dataset, insertAdjacentHTML, ProcessingInstruction, importNode/adoptNode,
// isEqualNode/isConnected, and phase-filtered event dispatch.

#include "test_framework.hpp"

#include "../include/dom/dom_enhancements.hpp"
#include "../include/dom/event_system.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace xiaopeng;
using namespace xiaopeng::dom;

// ── Shared tree (mirrors tests/test_dom_binding.cpp) ─────────
// <html><head><title>Test Page</title></head><body>
//   <div id="main" class="container active">
//     <p id="p1" class="text">Hello <span id="s1">World</span></p>
//     <p id="p2" class="text muted">Second</p>
//     <ul id="list"><li id="li1">Item 1</li><li id="li2">Item 2</li></ul>
//   </div>
// </body></html>

namespace {

std::shared_ptr<Element> makeEl(const std::shared_ptr<Document> &doc,
                                const std::string &tag,
                                const std::string &id = "",
                                const std::string &cls = "") {
  auto el = doc->createElement(tag);
  if (!id.empty())
    el->setId(id);
  if (!cls.empty())
    el->setClassName(cls);
  return el;
}

std::shared_ptr<Document> buildTestDOM() {
  auto doc = std::make_shared<Document>();

  auto html = makeEl(doc, "html");
  doc->appendChild(html);

  auto head = makeEl(doc, "head");
  html->appendChild(head);
  auto title = makeEl(doc, "title");
  title->appendChild(doc->createTextNode("Test Page"));
  head->appendChild(title);

  auto body = makeEl(doc, "body");
  html->appendChild(body);

  auto main = makeEl(doc, "div", "main", "container active");
  body->appendChild(main);

  auto p1 = makeEl(doc, "p", "p1", "text");
  p1->appendChild(doc->createTextNode("Hello "));
  auto span = makeEl(doc, "span", "s1");
  span->appendChild(doc->createTextNode("World"));
  p1->appendChild(span);
  main->appendChild(p1);

  auto p2 = makeEl(doc, "p", "p2", "text muted");
  p2->appendChild(doc->createTextNode("Second"));
  main->appendChild(p2);

  auto ul = makeEl(doc, "ul", "list");
  auto li1 = makeEl(doc, "li", "li1");
  li1->appendChild(doc->createTextNode("Item 1"));
  ul->appendChild(li1);
  auto li2 = makeEl(doc, "li", "li2");
  li2->appendChild(doc->createTextNode("Item 2"));
  ul->appendChild(li2);
  main->appendChild(ul);

  return doc;
}

std::vector<std::string> ids(const std::vector<ElementPtr> &elems) {
  std::vector<std::string> out;
  for (const auto &e : elems)
    out.push_back(e->id());
  return out;
}

bool hasId(const std::vector<ElementPtr> &elems, const std::string &id) {
  return std::find(ids(elems).begin(), ids(elems).end(), id) !=
         ids(elems).end();
}

} // namespace

// ── Selector engine ──────────────────────────────────────────

TEST(QuerySelector_TagAndCombinators) {
  auto doc = buildTestDOM();

  EXPECT_EQ(doc->querySelectorAll("p").size(), (size_t)2);
  EXPECT_EQ(doc->querySelectorAll("div p").size(), (size_t)2);
  EXPECT_EQ(doc->querySelectorAll("div > p").size(), (size_t)2);
  EXPECT_EQ(doc->querySelectorAll("body > p").size(), (size_t)0);
  EXPECT_EQ(doc->querySelectorAll("ul > li").size(), (size_t)2);
  EXPECT_EQ(doc->querySelectorAll("p span").size(), (size_t)1);
  EXPECT_EQ(doc->querySelectorAll("div > ul").size(), (size_t)1);
  EXPECT_EQ(doc->querySelectorAll("#p1 span").size(), (size_t)1);
  EXPECT_EQ(doc->querySelectorAll("div.container > ul").size(), (size_t)1);
}

TEST(QuerySelector_ClassesAndIds) {
  auto doc = buildTestDOM();

  EXPECT_EQ(doc->querySelectorAll(".text").size(), (size_t)2);
  auto byId = doc->querySelectorAll("#p1");
  ASSERT_EQ(byId.size(), (size_t)1);
  EXPECT_STREQ(byId[0]->id().c_str(), "p1");
  EXPECT_EQ(doc->querySelectorAll("p.text").size(), (size_t)2);
  EXPECT_EQ(doc->querySelectorAll(".container .text").size(), (size_t)2);

  auto first = doc->querySelector(".muted");
  EXPECT_TRUE(first && first->id() == "p2");
}

TEST(QuerySelector_AttributeOperators) {
  auto doc = buildTestDOM();

  EXPECT_EQ(doc->querySelectorAll("[id]").size(), (size_t)7);
  EXPECT_EQ(doc->querySelectorAll("[class]").size(), (size_t)3);
  EXPECT_EQ(doc->querySelectorAll("div[id=\"main\"]").size(), (size_t)1);
  EXPECT_EQ(doc->querySelectorAll("p[class~=\"muted\"]").size(), (size_t)1);
  EXPECT_EQ(doc->querySelectorAll("[class*=\"tain\"]").size(), (size_t)1);
  // li1, li2 and list all start with "li"
  EXPECT_EQ(doc->querySelectorAll("[id^=\"li\"]").size(), (size_t)3);
}

TEST(QuerySelector_PseudoClasses) {
  auto doc = buildTestDOM();

  auto first = doc->querySelectorAll("li:first-child");
  ASSERT_EQ(first.size(), (size_t)1);
  EXPECT_STREQ(first[0]->id().c_str(), "li1");

  auto last = doc->querySelectorAll("li:last-child");
  ASSERT_EQ(last.size(), (size_t)1);
  EXPECT_STREQ(last[0]->id().c_str(), "li2");

  auto nth = doc->querySelectorAll("li:nth-child(2)");
  ASSERT_EQ(nth.size(), (size_t)1);
  EXPECT_STREQ(nth[0]->id().c_str(), "li2");

  auto notMuted = doc->querySelectorAll("p:not(.muted)");
  ASSERT_EQ(notMuted.size(), (size_t)1);
  EXPECT_STREQ(notMuted[0]->id().c_str(), "p1");
}

TEST(QuerySelector_SelectorListAndRoot) {
  auto doc = buildTestDOM();

  auto list = doc->querySelectorAll("h1, p");
  EXPECT_EQ(list.size(), (size_t)2);

  // documentElement must be a candidate for document-scoped queries
  EXPECT_TRUE(doc->querySelector("html") != nullptr);
  EXPECT_TRUE(doc->querySelector("head") != nullptr);
  EXPECT_TRUE(doc->querySelector("body > div") != nullptr);
}

TEST(QuerySelector_ElementScopeAndMatchesAndClosest) {
  auto doc = buildTestDOM();
  auto main = doc->getElementById("main");
  auto s1 = doc->getElementById("s1");
  ASSERT_TRUE(main && s1);

  // Element scope excludes the element itself for querySelectorAll
  EXPECT_EQ(main->querySelectorAll("p").size(), (size_t)2);
  EXPECT_EQ(main->querySelectorAll("div").size(), (size_t)0);
  EXPECT_EQ(doc->querySelectorAll("#main").size(), (size_t)1);

  // matches has no scope restriction
  EXPECT_TRUE(main->matches("#main"));
  EXPECT_TRUE(main->matches("div.container"));
  EXPECT_TRUE(main->matches("div.container.active"));
  EXPECT_TRUE(s1->matches("span#s1"));

  EXPECT_TRUE(s1->closest(".container") == main);
  EXPECT_TRUE(s1->closest("body") != nullptr);
  EXPECT_TRUE(s1->closest("#nonexistent") == nullptr);
}

TEST(QuerySelector_InvalidReturnsEmpty) {
  auto doc = buildTestDOM();
  EXPECT_EQ(doc->querySelectorAll("").size(), (size_t)0);
  EXPECT_EQ(doc->querySelectorAll("   ").size(), (size_t)0);
  // Note: the CSS tokenizer is lenient, so malformed-but-tokenizable input
  // (e.g. "div >> p") does not produce an error — it matches best-effort.
}

// ── Document ─────────────────────────────────────────────────

TEST(Document_DocumentElementIsFirstElementChild) {
  auto doc = std::make_shared<Document>();
  auto svg = makeEl(doc, "svg");
  doc->appendChild(svg);
  EXPECT_TRUE(doc->documentElement() == svg);
  EXPECT_EQ(doc->querySelectorAll("svg").size(), (size_t)1);
  EXPECT_TRUE(doc->querySelector("svg") == svg);

  // html documents keep working
  auto htmlDoc = buildTestDOM();
  EXPECT_STREQ(htmlDoc->documentElement()->localName().c_str(), "html");
}

TEST(Document_ProcessingInstruction) {
  auto doc = std::make_shared<Document>();
  auto pi = doc->createProcessingInstruction("xml-stylesheet",
                                             "href=\"a.css\"");
  ASSERT_TRUE(pi != nullptr);
  EXPECT_TRUE(pi->nodeType() == NodeType::ProcessingInstruction);
  EXPECT_STREQ(pi->target().c_str(), "xml-stylesheet");
  EXPECT_STREQ(pi->data().c_str(), "href=\"a.css\"");
  EXPECT_STREQ(pi->toHtml().c_str(), "<?xml-stylesheet href=\"a.css\"?>");

  auto clone = pi->cloneNode(false);
  EXPECT_TRUE(clone->isEqualNode(pi));
}

TEST(Document_ImportAndAdopt) {
  auto doc = buildTestDOM();

  auto imported = doc->importNode(doc->getElementById("p1"), true);
  ASSERT_TRUE(imported != nullptr);
  EXPECT_TRUE(imported->nodeType() == NodeType::Element);
  EXPECT_STREQ(imported->textContent().c_str(), "Hello World");
  EXPECT_TRUE(imported->parentNode() == nullptr);

  auto adopted = doc->adoptNode(doc->getElementById("p2"));
  ASSERT_TRUE(adopted != nullptr);
  EXPECT_TRUE(adopted->parentNode() == nullptr);
  EXPECT_TRUE(doc->getElementById("p2") == nullptr);

  auto li1 = doc->getElementById("li1");
  EXPECT_EQ(doc->getElementsByName("x").size(), (size_t)0);
  li1->setAttribute("name", "item");
  EXPECT_EQ(doc->getElementsByName("item").size(), (size_t)1);
}

// ── Attributes ───────────────────────────────────────────────

TEST(Element_AttributeNamesHasAttributes) {
  auto doc = std::make_shared<Document>();
  auto el = makeEl(doc, "div");
  EXPECT_FALSE(el->hasAttributes());
  EXPECT_EQ(el->getAttributeNames().size(), (size_t)0);

  el->setAttribute("alpha", "1");
  el->setAttribute("beta", "2");
  EXPECT_TRUE(el->hasAttributes());
  auto names = el->getAttributeNames();
  ASSERT_EQ(names.size(), (size_t)2);
  EXPECT_STREQ(names[0].c_str(), "alpha");
  EXPECT_STREQ(names[1].c_str(), "beta");
}

TEST(Element_ToggleAttributeForce) {
  auto doc = std::make_shared<Document>();
  auto el = makeEl(doc, "div");

  EXPECT_TRUE(el->toggleAttribute("hidden", true));
  EXPECT_TRUE(el->hasAttribute("hidden"));

  EXPECT_FALSE(el->toggleAttribute("hidden", false));
  EXPECT_FALSE(el->hasAttribute("hidden"));

  // no-force toggles and returns the new presence
  el->toggleAttribute("open");
  EXPECT_TRUE(el->hasAttribute("open"));
  el->toggleAttribute("open");
  EXPECT_FALSE(el->hasAttribute("open"));
}

TEST(Element_DatasetRoundTrip) {
  auto doc = std::make_shared<Document>();
  auto el = makeEl(doc, "div");

  el->setDataset("userId", "42");
  el->setDataset("layoutMode", "grid");

  EXPECT_TRUE(el->hasAttribute("data-user-id"));
  EXPECT_TRUE(el->hasAttribute("data-layout-mode"));
  auto v = el->getDataset("userId");
  EXPECT_TRUE(v.has_value() && v.value() == "42");
  EXPECT_FALSE(el->getDataset("missing").has_value());

  auto keys = el->datasetKeys();
  ASSERT_EQ(keys.size(), (size_t)2);
  EXPECT_TRUE(std::find(keys.begin(), keys.end(), "userId") != keys.end());
  EXPECT_TRUE(std::find(keys.begin(), keys.end(), "layoutMode") != keys.end());

  el->deleteDataset("userId");
  EXPECT_FALSE(el->hasDataset("userId"));
  EXPECT_EQ(el->datasetKeys().size(), (size_t)1);
}

// ── Tree manipulation ────────────────────────────────────────

TEST(Node_AppendPrependReplaceChildren) {
  auto doc = std::make_shared<Document>();
  auto div = makeEl(doc, "div");

  div->append(makeEl(doc, "span", "a"));
  div->append(makeEl(doc, "span", "b"));
  EXPECT_EQ(div->childElementCount(), (size_t)2);

  div->prepend(makeEl(doc, "span", "c"));
  EXPECT_STREQ(div->firstElementChild()->id().c_str(), "c");

  auto frag = doc->createDocumentFragment();
  frag->appendChild(makeEl(doc, "span", "d"));
  frag->appendChild(makeEl(doc, "span", "e"));
  div->replaceChildren(frag);
  EXPECT_EQ(div->childElementCount(), (size_t)2);
  EXPECT_STREQ(div->firstElementChild()->id().c_str(), "d");
  EXPECT_STREQ(div->lastElementChild()->id().c_str(), "e");
  EXPECT_EQ(frag->childCount(), (size_t)0); // children moved out
}

TEST(Node_BeforeAfterReplaceWith) {
  auto doc = std::make_shared<Document>();
  auto parent = makeEl(doc, "div");
  auto a = makeEl(doc, "span", "a");
  auto b = makeEl(doc, "span", "b");
  auto c = makeEl(doc, "span", "c");
  parent->appendChild(a);
  parent->appendChild(b);

  a->after(c); // [a, c, b]
  EXPECT_STREQ(parent->firstElementChild()->id().c_str(), "a");
  EXPECT_STREQ(parent->lastElementChild()->id().c_str(), "b");

  b->before(a); // a moves before b → [c, a, b]
  EXPECT_STREQ(parent->firstElementChild()->id().c_str(), "c");

  auto frag = doc->createDocumentFragment();
  frag->appendChild(makeEl(doc, "span", "x"));
  frag->appendChild(makeEl(doc, "span", "y"));
  c->replaceWith(frag); // [x, y, a, b]
  EXPECT_EQ(parent->childElementCount(), (size_t)4);
  EXPECT_STREQ(parent->firstElementChild()->id().c_str(), "x");

  // after the last child appends at the end
  b->after(makeEl(doc, "span", "z"));
  EXPECT_STREQ(parent->lastElementChild()->id().c_str(), "z");

  // replaceWith without a parent is a no-op
  auto orphan = makeEl(doc, "span", "orphan");
  orphan->replaceWith(makeEl(doc, "span", "q"));
  EXPECT_TRUE(orphan->parentNode() == nullptr);
}

TEST(Node_RemoveAndIsConnected) {
  auto doc = buildTestDOM();
  auto p2 = doc->getElementById("p2");
  ASSERT_TRUE(p2 != nullptr);
  EXPECT_TRUE(p2->isConnected());

  p2->remove();
  EXPECT_FALSE(p2->isConnected());
  EXPECT_TRUE(doc->getElementById("p2") == nullptr);

  auto orphan = makeEl(doc, "div");
  EXPECT_FALSE(orphan->isConnected());
}

TEST(Node_IsEqualNode) {
  auto doc = std::make_shared<Document>();
  auto a = makeEl(doc, "div", "x", "c");
  a->appendChild(doc->createTextNode("hi"));
  auto b = makeEl(doc, "div", "x", "c");
  b->appendChild(doc->createTextNode("hi"));
  EXPECT_TRUE(a->isEqualNode(b));

  EXPECT_FALSE(a->isEqualNode(makeEl(doc, "div", "x", "other")));
  EXPECT_FALSE(a->isEqualNode(makeEl(doc, "p", "x", "c")));

  auto d = makeEl(doc, "div", "x", "c");
  d->appendChild(doc->createTextNode("bye"));
  EXPECT_FALSE(a->isEqualNode(d));

  auto e = makeEl(doc, "div", "x", "c");
  EXPECT_FALSE(a->isEqualNode(e)); // different child count
}

TEST(Node_ParentElementIgnoresDocument) {
  auto doc = buildTestDOM();
  auto html = doc->documentElement();
  EXPECT_TRUE(html->parentElement() == nullptr); // parent is the Document
  EXPECT_TRUE(doc->body()->parentElement() == html);
}

// ── CharacterData ────────────────────────────────────────────

TEST(Text_CharacterDataApi) {
  auto doc = std::make_shared<Document>();
  auto text = std::static_pointer_cast<TextNode>(doc->createTextNode("hello"));
  EXPECT_EQ(text->length(), (size_t)5);

  EXPECT_STREQ(text->substringData(1, 3).c_str(), "ell");
  text->appendData("!");
  EXPECT_STREQ(text->data().c_str(), "hello!");
  text->insertData(0, "X");
  EXPECT_STREQ(text->data().c_str(), "Xhello!");
  text->deleteData(0, 1);
  EXPECT_STREQ(text->data().c_str(), "hello!");
  text->replaceData(0, 5, "HELLO");
  EXPECT_STREQ(text->data().c_str(), "HELLO!");
}

TEST(Text_SplitText) {
  auto doc = std::make_shared<Document>();
  auto p = makeEl(doc, "p");
  auto first = std::static_pointer_cast<TextNode>(doc->createTextNode("abcdef"));
  p->appendChild(first);

  auto rest = std::static_pointer_cast<TextNode>(first->splitText(3));
  EXPECT_STREQ(first->data().c_str(), "abc");
  EXPECT_STREQ(rest->data().c_str(), "def");
  EXPECT_TRUE(rest->parentNode() == p);
  EXPECT_TRUE(first->nextSibling() == rest);
  EXPECT_EQ(p->childCount(), (size_t)2);
}

TEST(Comment_CharacterDataApi) {
  auto doc = std::make_shared<Document>();
  auto comment =
      std::static_pointer_cast<CommentNode>(doc->createComment("note"));
  comment->appendData("-more");
  EXPECT_STREQ(comment->data().c_str(), "note-more");
  EXPECT_STREQ(comment->substringData(0, 4).c_str(), "note");
  comment->deleteData(0, 5);
  EXPECT_STREQ(comment->data().c_str(), "more");
}

// ── HTML fragment mutation ───────────────────────────────────

TEST(Element_SetInnerHTML) {
  auto doc = std::make_shared<Document>();
  auto div = makeEl(doc, "div");
  div->appendChild(doc->createTextNode("old"));
  div->setInnerHTML("<p id=\"a\">Hi</p><span>there</span>");

  EXPECT_EQ(div->childElementCount(), (size_t)2);
  auto a = div->querySelector("#a");
  EXPECT_TRUE(a != nullptr);
  EXPECT_STREQ(a->textContent().c_str(), "Hi");
}

TEST(Element_SetOuterHTML) {
  auto doc = std::make_shared<Document>();
  auto parent = makeEl(doc, "div");
  auto a = makeEl(doc, "span", "a");
  auto b = makeEl(doc, "span", "b");
  parent->appendChild(a);
  parent->appendChild(b);

  a->setOuterHTML("<em id=\"x\">new</em>");
  EXPECT_EQ(parent->childElementCount(), (size_t)2);
  EXPECT_STREQ(parent->firstElementChild()->id().c_str(), "x");
  EXPECT_STREQ(parent->lastElementChild()->id().c_str(), "b");
}

TEST(Element_InsertAdjacentPositions) {
  auto doc = std::make_shared<Document>();
  auto parent = makeEl(doc, "div");
  auto mid = makeEl(doc, "span", "mid");
  parent->appendChild(mid);

  mid->insertAdjacentHTML("beforebegin", "<b id=\"bb\"></b>");
  mid->insertAdjacentHTML("afterbegin", "<i id=\"ab\"></i>");
  mid->insertAdjacentHTML("beforeend", "<u id=\"be\"></u>");
  mid->insertAdjacentHTML("afterend", "<em id=\"ae\"></em>");

  EXPECT_EQ(parent->childElementCount(), (size_t)3);
  EXPECT_STREQ(parent->firstElementChild()->id().c_str(), "bb");
  EXPECT_STREQ(parent->lastElementChild()->id().c_str(), "ae");
  EXPECT_STREQ(mid->firstElementChild()->id().c_str(), "ab");
  EXPECT_STREQ(mid->lastElementChild()->id().c_str(), "be");
}

// ── Event phases ─────────────────────────────────────────────

TEST(Event_ListenerPhaseLookup) {
  auto doc = std::make_shared<Document>();
  auto parent = makeEl(doc, "div");
  auto child = makeEl(doc, "span");
  parent->appendChild(child);

  child->addEventListener("click", 10, false); // bubble listener
  child->addEventListener("click", 11, true);  // capture listener
  parent->addEventListener("click", 20, false);

  auto atTarget = child->getListenerIdsForPhase("click", EventPhase::AtTarget);
  ASSERT_EQ(atTarget.size(), (size_t)2); // both kinds fire at target

  auto capturing = child->getListenerIdsForPhase("click", EventPhase::Capturing);
  ASSERT_EQ(capturing.size(), (size_t)1);
  EXPECT_EQ(capturing[0], 11u);

  auto bubbling = child->getListenerIdsForPhase("click", EventPhase::Bubbling);
  ASSERT_EQ(bubbling.size(), (size_t)1);
  EXPECT_EQ(bubbling[0], 10u);

  EXPECT_TRUE(
      parent->getListenerIdsForPhase("click", EventPhase::Capturing).empty());
  EXPECT_EQ(parent->getListenerIdsForPhase("click", EventPhase::Bubbling).size(),
            (size_t)1);

  parent->removeEventListener("click", 20);
  EXPECT_TRUE(
      parent->getListenerIdsForPhase("click", EventPhase::Bubbling).empty());
}

TEST(Event_PhaseFilteringEndToEnd) {
  auto doc = std::make_shared<Document>();
  auto parent = makeEl(doc, "div");
  auto child = makeEl(doc, "span");
  parent->appendChild(child);

  std::vector<std::string> log;
  EventSystem::setEventDispatchCallback(
      [&](NodePtr node, const std::shared_ptr<Event> &, EventPhase phase) {
        auto ids = node->getListenerIdsForPhase("click", phase);
        if (ids.empty())
          return;
        std::string name = node->nodeType() == NodeType::Element
                               ? std::static_pointer_cast<Element>(node)->localName()
                               : "?";
        log.push_back(name + ":" + std::to_string(static_cast<int>(phase)));
      });

  parent->addEventListener("click", 1, true);  // capture
  child->addEventListener("click", 2, false);  // bubble

  EventSystem::dispatchEvent(child->shared_from_this(),
                             std::make_shared<Event>("click", true, true));

  // Capture on parent, then target. The parent's capture listener must NOT
  // fire again during bubbling (regression: double invocation).
  ASSERT_EQ(log.size(), (size_t)2);
  EXPECT_STREQ(log[0].c_str(), "div:1");
  EXPECT_STREQ(log[1].c_str(), "span:2");

  EventSystem::setEventDispatchCallback(nullptr);
}

int main() {
  return xiaopeng::test::runTests();
}
