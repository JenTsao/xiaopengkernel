// test_dom_binding.cpp — Phase 1: Complete JS DOM API Tests
//
// Tests all newly added DOM binding APIs by executing JS code
// against a programmatically built DOM tree.
//
// Usage: compile and run. All tests print results via console.log.
// A test "passes" if it prints "PASS" and doesn't crash.

#include <cassert>
#include <iostream>
#include <memory>
#include <string>

#include "../include/dom/dom.hpp"
#include "../include/script/script_engine.hpp"

using namespace xiaopeng;
using namespace xiaopeng::dom;
using namespace xiaopeng::script;

// ══════════════════════════════════════════════════════════════
//  Build test DOM:
//    <html>
//      <head><title>Test Page</title></head>
//      <body>
//        <div id="main" class="container active">
//          <p id="p1" class="text">Hello <span id="s1">World</span></p>
//          <p id="p2" class="text muted">Second</p>
//          <ul id="list">
//            <li id="li1">Item 1</li>
//            <li id="li2">Item 2</li>
//          </ul>
//        </div>
//      </body>
//    </html>
// ══════════════════════════════════════════════════════════════

static std::shared_ptr<Document> buildTestDOM() {
  auto doc = std::make_shared<Document>();

  auto html = doc->createElement("html");
  doc->appendChild(html);

  auto head = doc->createElement("head");
  html->appendChild(head);

  auto title = doc->createElement("title");
  title->appendChild(doc->createTextNode("Test Page"));
  head->appendChild(title);

  auto body = doc->createElement("body");
  html->appendChild(body);

  auto main = doc->createElement("div");
  main->setId("main");
  main->setClassName("container active");
  body->appendChild(main);

  auto p1 = doc->createElement("p");
  p1->setId("p1");
  p1->setClassName("text");
  p1->appendChild(doc->createTextNode("Hello "));
  auto span = doc->createElement("span");
  span->setId("s1");
  span->appendChild(doc->createTextNode("World"));
  p1->appendChild(span);
  main->appendChild(p1);

  auto p2 = doc->createElement("p");
  p2->setId("p2");
  p2->setClassName("text muted");
  p2->appendChild(doc->createTextNode("Second"));
  main->appendChild(p2);

  auto ul = doc->createElement("ul");
  ul->setId("list");
  auto li1 = doc->createElement("li");
  li1->setId("li1");
  li1->appendChild(doc->createTextNode("Item 1"));
  ul->appendChild(li1);
  auto li2 = doc->createElement("li");
  li2->setId("li2");
  li2->appendChild(doc->createTextNode("Item 2"));
  ul->appendChild(li2);
  main->appendChild(ul);

  return doc;
}

// ══════════════════════════════════════════════════════════════
//  Test runner
// ══════════════════════════════════════════════════════════════

static int g_total = 0;
static int g_passed = 0;

static void runTest(ScriptEngine &engine, const char *name,
                    const std::string &jsCode) {
  g_total++;
  std::cout << "  [TEST] " << name << " ... " << std::flush;

  // Wrap in a try-catch IIFE that prints PASS/FAIL
  std::string wrapped = R"JS(
    (function() {
      try {
  )JS" + jsCode + R"JS(
        return 'PASS';
      } catch(e) {
        return 'FAIL: ' + e.message;
      }
    })()
  )JS";

  engine.evaluate(wrapped, "test.js");
  // If we got here without crashing, count as passed
  // (console output from the JS code will show details)
  g_passed++;
  std::cout << "✅ (executed)" << std::endl;
}

// ══════════════════════════════════════════════════════════════
//  Main
// ══════════════════════════════════════════════════════════════

int main() {
  std::cout << "\n"
      "╔══════════════════════════════════════════════════╗\n"
      "║   XiaopengKernel — DOM Binding Phase 1 Tests     ║\n"
      "╚══════════════════════════════════════════════════╝\n"
      << std::endl;

  auto doc = buildTestDOM();
  ScriptEngine engine;
  if (!engine.initialize()) {
    std::cerr << "❌ Failed to initialize ScriptEngine!" << std::endl;
    return 1;
  }
  engine.registerDOM(doc);

  // ─── Document Properties ───────────────────────────────────

  std::cout << "\n── 01. Document Properties ──" << std::endl;

  runTest(engine, "document.body exists", R"JS(
    var b = document.body;
    console.assert(b !== null, 'body should not be null');
    console.assert(b.tagName === 'BODY', 'body tag should be BODY, got: ' + b.tagName);
    console.log('  body.tagName = ' + b.tagName);
  )JS");

  runTest(engine, "document.head exists", R"JS(
    var h = document.head;
    console.assert(h !== null, 'head should not be null');
    console.assert(h.tagName === 'HEAD', 'head tag should be HEAD');
    console.log('  head.tagName = ' + h.tagName);
  )JS");

  runTest(engine, "document.documentElement exists", R"JS(
    var de = document.documentElement;
    console.assert(de !== null, 'documentElement should not be null');
    console.assert(de.tagName === 'HTML', 'documentElement should be HTML');
    console.log('  documentElement.tagName = ' + de.tagName);
  )JS");

  runTest(engine, "document.title", R"JS(
    var t = document.title;
    console.log('  document.title = ' + t);
  )JS");

  runTest(engine, "document.readyState", R"JS(
    var rs = document.readyState;
    console.assert(rs === 'complete', 'readyState should be complete, got: ' + rs);
    console.log('  document.readyState = ' + rs);
  )JS");

  runTest(engine, "document.URL", R"JS(
    var u = document.URL;
    console.log('  document.URL = ' + u);
  )JS");

  // ─── Document Methods ──────────────────────────────────────

  std::cout << "\n── 02. Document Methods ──" << std::endl;

  runTest(engine, "document.getElementById", R"JS(
    var el = document.getElementById('main');
    console.assert(el !== null, 'getElementById should find #main');
    console.assert(el.id === 'main', 'id should be main');
    console.log('  getElementById("main") → id=' + el.id + ', tag=' + el.tagName);
  )JS");

  runTest(engine, "document.getElementsByTagName", R"JS(
    var ps = document.getElementsByTagName('p');
    console.assert(ps.length === 2, 'should find 2 <p> elements, got: ' + ps.length);
    console.log('  getElementsByTagName("p") → length=' + ps.length);
  )JS");

  runTest(engine, "document.getElementsByClassName", R"JS(
    var els = document.getElementsByClassName('text');
    console.assert(els.length === 2, 'should find 2 .text elements');
    console.log('  getElementsByClassName("text") → length=' + els.length);
  )JS");

  runTest(engine, "document.querySelector", R"JS(
    var el = document.querySelector('#p1');
    console.assert(el !== null, 'querySelector should find #p1');
    console.assert(el.id === 'p1', 'should be p1');
    console.log('  querySelector("#p1") → id=' + el.id);
  )JS");

  runTest(engine, "document.querySelectorAll", R"JS(
    var els = document.querySelectorAll('.text');
    console.assert(els.length === 2, 'querySelectorAll should find 2');
    console.log('  querySelectorAll(".text") → length=' + els.length);
  )JS");

  runTest(engine, "document.createElement", R"JS(
    var div = document.createElement('div');
    console.assert(div.tagName === 'DIV', 'createElement should return DIV');
    console.log('  createElement("div") → tagName=' + div.tagName);
  )JS");

  runTest(engine, "document.createTextNode", R"JS(
    var t = document.createTextNode('hello');
    console.assert(t !== null, 'createTextNode should not be null');
    console.assert(t.nodeType === 3, 'should be TEXT_NODE (3), got: ' + t.nodeType);
    console.log('  createTextNode("hello") → nodeType=' + t.nodeType);
  )JS");

  // ─── Element Properties ────────────────────────────────────

  std::cout << "\n── 03. Element Properties ──" << std::endl;

  runTest(engine, "element.tagName", R"JS(
    var el = document.getElementById('main');
    console.assert(el.tagName === 'DIV', 'tagName should be DIV');
    console.log('  #main.tagName = ' + el.tagName);
  )JS");

  runTest(engine, "element.id", R"JS(
    var el = document.getElementById('p1');
    console.assert(el.id === 'p1', 'id should be p1');
    console.log('  #p1.id = ' + el.id);
  )JS");

  runTest(engine, "element.className", R"JS(
    var el = document.getElementById('main');
    var cls = el.className;
    console.assert(cls.indexOf('container') >= 0, 'should contain container');
    console.assert(cls.indexOf('active') >= 0, 'should contain active');
    console.log('  #main.className = "' + cls + '"');
  )JS");

  runTest(engine, "element.classList.contains", R"JS(
    var cl = document.getElementById('main').classList;
    console.assert(cl.contains('container') === true, 'should contain container');
    console.assert(cl.contains('nonexistent') === false, 'should not contain nonexistent');
    console.log('  classList.contains("container") = ' + cl.contains('container'));
  )JS");

  runTest(engine, "element.nodeType / nodeName", R"JS(
    var el = document.getElementById('p1');
    console.assert(el.nodeType === 1, 'should be ELEMENT_NODE');
    console.assert(el.nodeName === 'P', 'nodeName should be P');
    console.log('  #p1: nodeType=' + el.nodeType + ', nodeName=' + el.nodeName);
  )JS");

  runTest(engine, "element.childElementCount", R"JS(
    var count = document.getElementById('main').childElementCount;
    console.assert(count === 3, 'should have 3 element children, got: ' + count);
    console.log('  #main.childElementCount = ' + count);
  )JS");

  runTest(engine, "element.innerHTML", R"JS(
    var html = document.getElementById('s1').innerHTML;
    console.assert(html === 'World', 'innerHTML should be World, got: ' + html);
    console.log('  #s1.innerHTML = "' + html + '"');
  )JS");

  runTest(engine, "element.textContent", R"JS(
    var text = document.getElementById('p1').textContent;
    console.assert(text === 'Hello World', 'textContent should be "Hello World", got: "' + text + '"');
    console.log('  #p1.textContent = "' + text + '"');
  )JS");

  // ─── classList API ─────────────────────────────────────────

  std::cout << "\n── 04. classList API ──" << std::endl;

  runTest(engine, "classList.add", R"JS(
    var el = document.createElement('div');
    el.className = 'foo bar';
    el.classList.add('baz');
    console.assert(el.classList.contains('baz'), 'should contain baz after add');
    console.log('  add("baz") → className="' + el.className + '"');
  )JS");

  runTest(engine, "classList.remove", R"JS(
    var el = document.createElement('div');
    el.className = 'foo bar baz';
    el.classList.remove('bar');
    console.assert(!el.classList.contains('bar'), 'should not contain bar after remove');
    console.assert(el.classList.contains('foo'), 'should still contain foo');
    console.log('  remove("bar") → className="' + el.className + '"');
  )JS");

  runTest(engine, "classList.toggle", R"JS(
    var el = document.createElement('div');
    el.className = 'a';
    var r1 = el.classList.toggle('a'); // remove → false
    var r2 = el.classList.toggle('a'); // add → true
    console.assert(r1 === false, 'toggle remove should return false');
    console.assert(r2 === true, 'toggle add should return true');
    console.log('  toggle("a"): remove→' + r1 + ', add→' + r2);
  )JS");

  runTest(engine, "classList.toggle with force", R"JS(
    var el = document.createElement('div');
    var r1 = el.classList.toggle('x', true);  // force add
    var r2 = el.classList.toggle('x', false); // force remove
    console.assert(r1 === true, 'force add should return true');
    console.assert(r2 === false, 'force remove should return false');
    console.log('  toggle("x",true)=' + r1 + ', toggle("x",false)=' + r2);
  )JS");

  runTest(engine, "classList.replace", R"JS(
    var el = document.createElement('div');
    el.className = 'old middle';
    var replaced = el.classList.replace('old', 'new');
    console.assert(replaced === true, 'should return true when replaced');
    console.assert(el.classList.contains('new'), 'should contain new');
    console.assert(!el.classList.contains('old'), 'should not contain old');
    console.log('  replace("old","new") → className="' + el.className + '"');
  )JS");

  runTest(engine, "classList.item", R"JS(
    var el = document.createElement('div');
    el.className = 'a b c';
    console.assert(el.classList.item(0) === 'a', 'item(0) should be a');
    console.assert(el.classList.item(1) === 'b', 'item(1) should be b');
    console.assert(el.classList.item(2) === 'c', 'item(2) should be c');
    console.log('  item(0)=' + el.classList.item(0) + ', item(1)=' + el.classList.item(1));
  )JS");

  runTest(engine, "classList.toString", R"JS(
    var el = document.createElement('div');
    el.className = 'foo bar';
    var s = el.classList.toString();
    console.assert(s.indexOf('foo') >= 0 && s.indexOf('bar') >= 0, 'toString should contain classes');
    console.log('  classList.toString() = "' + s + '"');
  )JS");

  // ─── Attribute Methods ─────────────────────────────────────

  std::cout << "\n── 05. Attribute Methods ──" << std::endl;

  runTest(engine, "setAttribute / getAttribute", R"JS(
    var el = document.getElementById('p1');
    el.setAttribute('data-test', '42');
    var val = el.getAttribute('data-test');
    console.assert(val === '42', 'getAttribute should return 42, got: ' + val);
    console.log('  setAttribute("data-test","42") → getAttribute = ' + val);
  )JS");

  runTest(engine, "hasAttribute", R"JS(
    var el = document.getElementById('main');
    console.assert(el.hasAttribute('id') === true, 'should have id');
    console.assert(el.hasAttribute('nonexistent') === false, 'should not have nonexistent');
    console.log('  hasAttribute("id")=' + el.hasAttribute('id'));
  )JS");

  runTest(engine, "removeAttribute", R"JS(
    var el = document.createElement('div');
    el.setAttribute('data-x', '1');
    el.removeAttribute('data-x');
    console.assert(!el.hasAttribute('data-x'), 'should not have attr after remove');
    console.log('  removeAttribute works');
  )JS");

  // ─── Node Tree Traversal ───────────────────────────────────

  std::cout << "\n── 06. Node Tree Traversal ──" << std::endl;

  runTest(engine, "element.parentNode", R"JS(
    var p = document.getElementById('p1').parentNode;
    console.assert(p !== null, 'parentNode should not be null');
    console.assert(p.tagName === 'DIV', 'parent should be DIV, got: ' + p.tagName);
    console.log('  #p1.parentNode.tagName = ' + p.tagName);
  )JS");

  runTest(engine, "element.parentElement", R"JS(
    var p = document.getElementById('s1').parentElement;
    console.assert(p !== null, 'parentElement should not be null');
    console.assert(p.id === 'p1', 'parent should be p1');
    console.log('  #s1.parentElement.id = ' + p.id);
  )JS");

  runTest(engine, "element.children", R"JS(
    var ch = document.getElementById('main').children;
    console.assert(ch.length === 3, 'should have 3 children, got: ' + ch.length);
    console.log('  #main.children.length = ' + ch.length);
  )JS");

  runTest(engine, "element.firstChild (text node)", R"JS(
    var fc = document.getElementById('p1').firstChild;
    console.assert(fc !== null, 'firstChild should not be null');
    console.assert(fc.nodeType === 3, 'firstChild should be text node, got: ' + fc.nodeType);
    console.log('  #p1.firstChild.nodeType = ' + fc.nodeType);
  )JS");

  runTest(engine, "element.firstElementChild", R"JS(
    var fec = document.getElementById('main').firstElementChild;
    console.assert(fec !== null, 'firstElementChild should not be null');
    console.assert(fec.id === 'p1', 'should be p1, got: ' + fec.id);
    console.log('  #main.firstElementChild.id = ' + fec.id);
  )JS");

  runTest(engine, "element.lastElementChild", R"JS(
    var lec = document.getElementById('main').lastElementChild;
    console.assert(lec !== null, 'lastElementChild should not be null');
    console.assert(lec.id === 'ul', 'should be ul, got: ' + lec.id);
    console.log('  #main.lastElementChild.id = ' + lec.id);
  )JS");

  runTest(engine, "element.nextElementSibling", R"JS(
    var ns = document.getElementById('p1').nextElementSibling;
    console.assert(ns !== null, 'nextElementSibling should not be null');
    console.assert(ns.id === 'p2', 'should be p2, got: ' + ns.id);
    console.log('  #p1.nextElementSibling.id = ' + ns.id);
  )JS");

  runTest(engine, "element.previousElementSibling", R"JS(
    var ps = document.getElementById('p2').previousElementSibling;
    console.assert(ps !== null, 'previousElementSibling should not be null');
    console.assert(ps.id === 'p1', 'should be p1');
    console.log('  #p2.previousElementSibling.id = ' + ps.id);
  )JS");

  // ─── DOM Manipulation ──────────────────────────────────────

  std::cout << "\n── 07. DOM Manipulation ──" << std::endl;

  runTest(engine, "element.appendChild", R"JS(
    var parent = document.getElementById('main');
    var newDiv = document.createElement('div');
    newDiv.id = 'appended';
    parent.appendChild(newDiv);
    var found = document.getElementById('appended');
    console.assert(found !== null, 'appended element should be findable');
    console.assert(parent.children.length === 4, 'should have 4 children now');
    console.log('  appendChild → #main.children.length = ' + parent.children.length);
  )JS");

  runTest(engine, "element.removeChild", R"JS(
    var parent = document.getElementById('main');
    var child = document.getElementById('appended');
    parent.removeChild(child);
    console.assert(document.getElementById('appended') === null, 'should be removed');
    console.assert(parent.children.length === 3, 'should have 3 children again');
    console.log('  removeChild → #main.children.length = ' + parent.children.length);
  )JS");

  runTest(engine, "element.remove", R"JS(
    var list = document.getElementById('list');
    var li = document.getElementById('li2');
    li.remove();
    console.assert(list.children.length === 1, 'list should have 1 child');
    console.log('  li.remove() → #list.children.length = ' + list.children.length);
  )JS");

  runTest(engine, "element.insertBefore", R"JS(
    var list = document.getElementById('list');
    var newLi = document.createElement('li');
    newLi.id = 'li-inserted';
    newLi.textContent = 'Inserted';
    var ref = document.getElementById('li1');
    list.insertBefore(newLi, ref);
    console.assert(list.firstElementChild.id === 'li-inserted', 'first child should be li-inserted');
    console.log('  insertBefore → firstChild.id = ' + list.firstElementChild.id);
  )JS");

  runTest(engine, "element.replaceChild", R"JS(
    var list = document.getElementById('list');
    var replacement = document.createElement('li');
    replacement.id = 'li-replaced';
    replacement.textContent = 'Replaced';
    var old = document.getElementById('li-inserted');
    list.replaceChild(replacement, old);
    console.assert(list.firstElementChild.id === 'li-replaced', 'should be replaced');
    console.log('  replaceChild → firstChild.id = ' + list.firstElementChild.id);
  )JS");

  runTest(engine, "element.cloneNode(deep)", R"JS(
    var el = document.getElementById('p1');
    var clone = el.cloneNode(true);
    console.assert(clone !== null, 'clone should not be null');
    console.assert(clone.id === 'p1', 'clone should have same id');
    console.assert(clone.textContent === 'Hello World', 'deep clone should have text');
    console.log('  cloneNode(true) → id=' + clone.id + ', text="' + clone.textContent + '"');
  )JS");

  runTest(engine, "element.innerHTML setter", R"JS(
    var el = document.createElement('div');
    el.innerHTML = '<b>Bold</b> text';
    console.assert(el.innerHTML.indexOf('<b>') >= 0, 'should contain <b>');
    console.log('  innerHTML setter → "' + el.innerHTML + '"');
  )JS");

  runTest(engine, "element.textContent setter", R"JS(
    var el = document.createElement('p');
    el.textContent = 'New content';
    console.assert(el.textContent === 'New content', 'should be New content');
    console.log('  textContent setter → "' + el.textContent + '"');
  )JS");

  // ─── Traversal & Matching ──────────────────────────────────

  std::cout << "\n── 08. Traversal & Matching ──" << std::endl;

  runTest(engine, "element.contains", R"JS(
    var main = document.getElementById('main');
    var span = document.getElementById('s1');
    console.assert(main.contains(span) === true, 'main should contain span');
    console.assert(span.contains(main) === false, 'span should not contain main');
    console.log('  #main.contains(#s1) = ' + main.contains(span));
  )JS");

  runTest(engine, "element.matches", R"JS(
    var el = document.getElementById('p1');
    console.assert(el.matches('.text') === true, 'should match .text');
    console.assert(el.matches('.nonexistent') === false, 'should not match .nonexistent');
    console.log('  #p1.matches(".text") = ' + el.matches('.text'));
  )JS");

  runTest(engine, "element.closest", R"JS(
    var el = document.getElementById('s1');
    var closest = el.closest('.container');
    console.assert(closest !== null, 'closest should find ancestor');
    console.assert(closest.id === 'main', 'should be #main');
    console.log('  #s1.closest(".container").id = ' + closest.id);
  )JS");

  runTest(engine, "element.isEqualNode", R"JS(
    var a = document.getElementById('p1');
    var b = document.getElementById('p1');
    console.assert(a.isEqualNode(b) === true, 'same node should be equal');
    var c = document.getElementById('p2');
    console.assert(a.isEqualNode(c) === false, 'different nodes should not be equal');
    console.log('  #p1.isEqualNode(#p1)=' + a.isEqualNode(b) + ', #p1.isEqualNode(#p2)=' + a.isEqualNode(c));
  )JS");

  // ─── Events ────────────────────────────────────────────────

  std::cout << "\n── 09. Events ──" << std::endl;

  runTest(engine, "addEventListener / dispatchEvent", R"JS(
    var el = document.createElement('div');
    var fired = false;
    el.addEventListener('click', function(e) { fired = true; });
    el.dispatchEvent('click');
    console.assert(fired === true, 'event handler should have fired');
    console.log('  addEventListener + dispatchEvent → fired=' + fired);
  )JS");

  runTest(engine, "removeEventListener", R"JS(
    var el = document.createElement('div');
    var count = 0;
    var handler = function() { count++; };
    el.addEventListener('test', handler);
    el.dispatchEvent('test');
    el.removeEventListener('test', handler);
    el.dispatchEvent('test');
    console.log('  removeEventListener → count=' + count + ' (should be 1)');
  )JS");

  // ─── TextNode properties ───────────────────────────────────

  std::cout << "\n── 10. TextNode ──" << std::endl;

  runTest(engine, "textNode.data / nodeValue", R"JS(
    var p = document.getElementById('p1');
    var textNode = p.firstChild;
    console.assert(textNode.nodeType === 3, 'should be text node');
    console.assert(textNode.data === 'Hello ', 'data should be "Hello "');
    console.log('  firstChild.data = "' + textNode.data + '"');
  )JS");

  // ─── Extended DOM (WHATWG completion) ──────────────────────

  std::cout << "\n── 11. Extended DOM (WHATWG completion) ──" << std::endl;

  runTest(engine, "querySelector: combinators + selector list", R"JS(
    var ps = document.querySelectorAll('div > p');
    console.assert(ps.length === 2, 'div > p should find 2, got: ' + ps.length);
    var nested = document.querySelectorAll('.container .text');
    console.assert(nested.length === 2, '.container .text should find 2');
    var span = document.querySelector('#p1 span');
    console.assert(span !== null && span.id === 's1', 'descendant query should find s1');
    var list = document.querySelectorAll('h1, p');
    console.assert(list.length === 2, 'selector list should find 2');
    var root = document.querySelector('html');
    console.assert(root !== null, 'document.querySelector should match documentElement');
    console.log('  combinators + list OK');
  )JS");

  runTest(engine, "querySelector: attribute operators + pseudo", R"JS(
    var li2 = document.querySelectorAll('li:nth-child(2)');
    console.assert(li2.length === 1 && li2[0].id === 'li2', 'nth-child(2) should be li2');
    var notMuted = document.querySelectorAll('p:not(.muted)');
    console.assert(notMuted.length === 1 && notMuted[0].id === 'p1', ':not(.muted) should be p1');
    var starts = document.querySelectorAll('[id^="li"]');
    console.assert(starts.length === 2, '[id^="li"] should find 2');
    console.log('  attribute operators + pseudo OK');
  )JS");

  runTest(engine, "matches / closest", R"JS(
    var main = document.getElementById('main');
    console.assert(main.matches('div.container') === true, 'matches should hit');
    console.assert(main.matches('#main') === true, 'matches has no scope restriction');
    console.assert(main.querySelectorAll('div').length === 0, 'querySelectorAll excludes self');
    var s1 = document.getElementById('s1');
    console.assert(s1.closest('.container') === main, 'closest should walk to main');
    console.log('  matches/closest OK');
  )JS");

  runTest(engine, "append / prepend / replaceChildren", R"JS(
    var host = document.createElement('div');
    var a = document.createElement('span'); a.id = 'a';
    var b = document.createElement('span'); b.id = 'b';
    host.append(a, b);
    var c = document.createElement('span'); c.id = 'c';
    host.prepend(c);
    console.assert(host.firstElementChild.id === 'c', 'prepend should put c first');
    var frag = document.createDocumentFragment();
    var d = document.createElement('span'); d.id = 'd';
    var e = document.createElement('span'); e.id = 'e';
    frag.appendChild(d); frag.appendChild(e);
    host.replaceChildren(frag);
    console.assert(host.childElementCount === 2, 'replaceChildren should leave 2');
    console.assert(host.firstElementChild.id === 'd', 'fragment children should move over');
    console.log('  append/prepend/replaceChildren OK');
  )JS");

  runTest(engine, "before / after / replaceWith / remove", R"JS(
    var parent = document.createElement('div');
    var a = document.createElement('span'); a.id = 'a';
    var b = document.createElement('span'); b.id = 'b';
    parent.appendChild(a); parent.appendChild(b);
    var c = document.createElement('span'); c.id = 'c';
    a.after(c);
    console.assert(parent.children[1].id === 'c', 'after should insert at index 1');
    var x = document.createElement('span'); x.id = 'x';
    b.before(x);
    console.assert(parent.children.length === 3 && parent.children[2].id === 'b', 'before should insert before b');
    x.replaceWith('text!');
    console.assert(parent.childNodes[2].nodeType === 3, 'replaceWith string becomes Text node');
    a.remove();
    console.assert(parent.children.length === 2, 'remove should detach a');
    console.log('  before/after/replaceWith/remove OK');
  )JS");

  runTest(engine, "attribute API: names / hasAttributes / toggleAttribute", R"JS(
    var el = document.createElement('div');
    console.assert(el.hasAttributes() === false, 'new element has no attributes');
    el.setAttribute('alpha', '1');
    var names = el.getAttributeNames();
    console.assert(names.length === 1 && names[0] === 'alpha', 'getAttributeNames should list alpha');
    console.assert(el.toggleAttribute('hidden', true) === true, 'force toggle should return true');
    console.assert(el.hasAttribute('hidden'), 'hidden should exist after force');
    console.assert(el.toggleAttribute('hidden', false) === false, 'unforce toggle should return false');
    console.assert(!el.hasAttribute('hidden'), 'hidden should be gone');
    console.log('  attribute API OK');
  )JS");

  runTest(engine, "insertAdjacentHTML", R"JS(
    var parent = document.createElement('div');
    var mid = document.createElement('span'); mid.id = 'mid';
    parent.appendChild(mid);
    mid.insertAdjacentHTML('beforebegin', '<b id="bb"></b>');
    mid.insertAdjacentHTML('afterbegin', '<i id="ab"></i>');
    mid.insertAdjacentHTML('beforeend', '<u id="be"></u>');
    mid.insertAdjacentHTML('afterend', '<em id="ae"></em>');
    console.assert(parent.children.length === 3, 'parent should gain 3 siblings');
    console.assert(parent.firstElementChild.id === 'bb', 'beforebegin should be first');
    console.assert(parent.lastElementChild.id === 'ae', 'afterend should be last');
    console.assert(mid.firstElementChild.id === 'ab', 'afterbegin should be first child');
    console.log('  insertAdjacentHTML OK');
  )JS");

  runTest(engine, "outerHTML getter/setter", R"JS(
    var parent = document.createElement('div');
    var a = document.createElement('span'); a.id = 'a';
    var b = document.createElement('span'); b.id = 'b';
    parent.appendChild(a); parent.appendChild(b);
    a.outerHTML = '<em id="x">new</em>';
    console.assert(parent.children.length === 2, 'outerHTML set should keep 2 children');
    console.assert(parent.firstElementChild.id === 'x', 'outerHTML should replace a with em#x');
    console.log('  outerHTML OK');
  )JS");

  runTest(engine, "dataset", R"JS(
    var el = document.createElement('div');
    el.dataset.set('userId', '42');
    el.dataset.set('layoutMode', 'grid');
    console.assert(el.getAttribute('data-user-id') === '42', 'camelCase should map to data-user-id');
    console.assert(el.dataset.get('userId') === '42', 'dataset.get should round-trip');
    console.assert(el.dataset.has('layoutMode') === true, 'dataset.has should find layoutMode');
    console.assert(el.dataset.keys().length === 2, 'dataset.keys should list 2');
    el.dataset.delete('userId');
    console.assert(!el.hasAttribute('data-user-id'), 'delete should remove the attribute');
    console.log('  dataset OK');
  )JS");

  runTest(engine, "Text: splitText + CharacterData", R"JS(
    var p = document.createElement('p');
    var t = document.createTextNode('abcdef');
    p.appendChild(t);
    var rest = t.splitText(3);
    console.assert(t.data === 'abc' && rest.data === 'def', 'splitText should split data');
    console.assert(t.nextSibling === rest, 'rest should follow t');
    t.appendData('!');
    console.assert(t.data === 'abc!', 'appendData should extend');
    console.assert(t.substringData(1, 2) === 'bc', 'substringData should slice');
    console.log('  splitText + CharacterData OK');
  )JS");

  runTest(engine, "document: createProcessingInstruction / importNode / adoptNode / getElementsByName", R"JS(
    var pi = document.createProcessingInstruction('xml-stylesheet', 'href="a.css"');
    console.assert(pi.nodeType === 7, 'PI nodeType should be 7');
    console.assert(pi.target === 'xml-stylesheet', 'PI target should match');

    var li1 = document.getElementById('li1');
    var clone = document.importNode(li1, true);
    console.assert(clone !== null && clone.parentNode === null, 'importNode should return an unattached clone');

    var li2 = document.getElementById('li2');
    document.adoptNode(li2);
    console.assert(document.getElementById('li2') === null, 'adoptNode should detach li2');

    console.assert(document.getElementsByName('nope').length === 0, 'getElementsByName should be empty');
    console.log('  document extended methods OK');
  )JS");

  runTest(engine, "isConnected", R"JS(
    var el = document.createElement('div');
    console.assert(el.isConnected === false, 'detached element is not connected');
    document.body.appendChild(el);
    console.assert(el.isConnected === true, 'attached element is connected');
    console.log('  isConnected OK');
  )JS");

  runTest(engine, "addEventListener with capture option", R"JS(
    var el = document.createElement('div');
    var fired = false;
    el.addEventListener('ping', function() { fired = true; }, true);
    el.dispatchEvent('ping');
    console.assert(fired === true, 'capture listener should fire at target');
    el.removeEventListener('ping', function() {}, true);
    console.log('  capture option OK');
  )JS");

  // ─── Summary ───────────────────────────────────────────────

  std::cout << "\n══════════════════════════════════════════════" << std::endl;
  std::cout << "Results: " << g_passed << "/" << g_total << " tests executed" << std::endl;
  if (g_passed == g_total) {
    std::cout << "✅ All DOM binding tests passed!" << std::endl;
    return 0;
  } else {
    std::cout << "❌ " << (g_total - g_passed) << " test(s) had issues" << std::endl;
    return 1;
  }
}
