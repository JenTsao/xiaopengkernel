#include "test_framework.hpp"
#include "loader/loader_all.hpp"
#include "dom/dom_enhancements.hpp"

using namespace xiaopeng::loader;
using namespace xiaopeng::dom;

TEST(Tokenizer_EmptyInput) {
    HtmlTokenizer tokenizer(std::string(""));
    Token token = tokenizer.nextToken();
    EXPECT_TRUE(token.isEndOfFile());
}

TEST(Tokenizer_SimpleText) {
    HtmlTokenizer tokenizer(std::string("Hello World"));
    std::vector<Token> tokens = tokenizer.tokenize();
    
    EXPECT_EQ(tokens.size(), 2);
    EXPECT_TRUE(tokens[0].isCharacter());
    EXPECT_STREQ(tokens[0].data.c_str(), "Hello World");
    EXPECT_TRUE(tokens[1].isEndOfFile());
}

TEST(Tokenizer_SimpleStartTag) {
    HtmlTokenizer tokenizer(std::string("<div>"));
    std::vector<Token> tokens = tokenizer.tokenize();
    
    EXPECT_EQ(tokens.size(), 2);
    EXPECT_TRUE(tokens[0].isStartTag());
    EXPECT_STREQ(tokens[0].name.c_str(), "div");
    EXPECT_TRUE(tokens[1].isEndOfFile());
}

TEST(Tokenizer_StartTagWithAttributes) {
    HtmlTokenizer tokenizer(std::string("<div id=\"test\" class=\"container\">"));
    std::vector<Token> tokens = tokenizer.tokenize();
    
    EXPECT_EQ(tokens.size(), 2);
    EXPECT_TRUE(tokens[0].isStartTag());
    EXPECT_STREQ(tokens[0].name.c_str(), "div");
    EXPECT_EQ(tokens[0].attributes.size(), 2);
    
    EXPECT_STREQ(tokens[0].attributes[0].name.c_str(), "id");
    EXPECT_STREQ(tokens[0].attributes[0].value.c_str(), "test");
    EXPECT_STREQ(tokens[0].attributes[1].name.c_str(), "class");
    EXPECT_STREQ(tokens[0].attributes[1].value.c_str(), "container");
}

TEST(Tokenizer_SelfClosingTag) {
    HtmlTokenizer tokenizer(std::string("<img src=\"test.png\" />"));
    std::vector<Token> tokens = tokenizer.tokenize();
    
    EXPECT_EQ(tokens.size(), 2);
    EXPECT_TRUE(tokens[0].isStartTag());
    EXPECT_STREQ(tokens[0].name.c_str(), "img");
    EXPECT_TRUE(tokens[0].selfClosing);
}

TEST(Tokenizer_EndTag) {
    HtmlTokenizer tokenizer(std::string("</div>"));
    std::vector<Token> tokens = tokenizer.tokenize();
    
    EXPECT_EQ(tokens.size(), 2);
    EXPECT_TRUE(tokens[0].isEndTag());
    EXPECT_STREQ(tokens[0].name.c_str(), "div");
}

TEST(Tokenizer_Comment) {
    HtmlTokenizer tokenizer(std::string("<!-- This is a comment -->"));
    std::vector<Token> tokens = tokenizer.tokenize();
    
    EXPECT_EQ(tokens.size(), 2);
    EXPECT_TRUE(tokens[0].isComment());
    EXPECT_STREQ(tokens[0].data.c_str(), " This is a comment ");
}

TEST(Tokenizer_Doctype) {
    HtmlTokenizer tokenizer(std::string("<!DOCTYPE html>"));
    std::vector<Token> tokens = tokenizer.tokenize();
    
    EXPECT_EQ(tokens.size(), 2);
    EXPECT_TRUE(tokens[0].isDoctype());
    EXPECT_STREQ(tokens[0].name.c_str(), "html");
}

TEST(Tokenizer_ComplexDocument) {
    std::string html = "<!DOCTYPE html><html><head><title>Test</title></head>"
                       "<body><div id=\"main\"><p>Hello</p></div></body></html>";
    HtmlTokenizer tokenizer(html);
    std::vector<Token> tokens = tokenizer.tokenize();
    
    EXPECT_GT(tokens.size(), 10);
    
    EXPECT_TRUE(tokens[0].isDoctype());
    EXPECT_TRUE(tokens[1].isStartTag());
    EXPECT_STREQ(tokens[1].name.c_str(), "html");
}

TEST(Tokenizer_CharacterReference) {
    HtmlTokenizer tokenizer(std::string("<div>&amp;&lt;&gt;</div>"));
    std::vector<Token> tokens = tokenizer.tokenize();
    
    EXPECT_GT(tokens.size(), 2);
    
    bool foundAmp = false, foundLt = false, foundGt = false;
    for (const auto& token : tokens) {
        if (token.isCharacter()) {
            if (token.data == "&") foundAmp = true;
            if (token.data == "<") foundLt = true;
            if (token.data == ">") foundGt = true;
        }
    }
    EXPECT_TRUE(foundAmp);
    EXPECT_TRUE(foundLt);
    EXPECT_TRUE(foundGt);
}

TEST(TreeBuilder_EmptyDocument) {
    HtmlTreeBuilder builder;
    auto doc = builder.build(std::string(""));
    
    EXPECT_TRUE(doc != nullptr);
    EXPECT_EQ(doc->nodeType(), NodeType::Document);
}

TEST(TreeBuilder_SimpleDocument) {
    std::string html = "<!DOCTYPE html><html><head><title>Test</title></head>"
                       "<body><p>Hello World</p></body></html>";
    HtmlTreeBuilder builder;
    auto doc = builder.build(html);
    
    EXPECT_TRUE(doc != nullptr);
    
    auto htmlElem = doc->documentElement();
    EXPECT_TRUE(htmlElem != nullptr);
    EXPECT_STREQ(htmlElem->localName().c_str(), "html");
    
    auto head = doc->head();
    EXPECT_TRUE(head != nullptr);
    
    auto body = doc->body();
    EXPECT_TRUE(body != nullptr);
    
    EXPECT_STREQ(doc->titleText().c_str(), "Test");
}

TEST(TreeBuilder_ElementCreation) {
    HtmlTreeBuilder builder;
    auto doc = builder.build(std::string("<div id=\"test\" class=\"container\"><span>Text</span></div>"));
    
    EXPECT_TRUE(doc != nullptr);
    
    auto body = doc->body();
    EXPECT_TRUE(body != nullptr);
    
    auto divs = doc->getElementsByTagName("div");
    EXPECT_EQ(divs.size(), 1);
    
    auto div = divs[0];
    EXPECT_STREQ(div->getAttribute("id")->c_str(), "test");
    EXPECT_STREQ(div->getAttribute("class")->c_str(), "container");
    
    auto spans = div->getElementsByTagName("span");
    EXPECT_EQ(spans.size(), 1);
}

TEST(TreeBuilder_NestedElements) {
    HtmlTreeBuilder builder;
    auto doc = builder.build(std::string("<div><p><span><strong>Bold</strong></span></p></div>"));
    
    auto strongs = doc->getElementsByTagName("strong");
    EXPECT_EQ(strongs.size(), 1);
    
    EXPECT_STREQ(strongs[0]->textContent().c_str(), "Bold");
}

TEST(TreeBuilder_ImplicitTags) {
    HtmlTreeBuilder builder;
    auto doc = builder.build(std::string("<p>Text</p>"));
    
    auto html = doc->documentElement();
    EXPECT_TRUE(html != nullptr);
    
    auto head = doc->head();
    EXPECT_TRUE(head != nullptr);
    
    auto body = doc->body();
    EXPECT_TRUE(body != nullptr);
}

TEST(TreeBuilder_TableStructure) {
    std::string html = "<table><thead><tr><th>Header</th></tr></thead>"
                       "<tbody><tr><td>Cell</td></tr></tbody></table>";
    HtmlTreeBuilder builder;
    auto doc = builder.build(html);
    
    auto tables = doc->getElementsByTagName("table");
    EXPECT_EQ(tables.size(), 1);
    
    auto rows = doc->getElementsByTagName("tr");
    EXPECT_EQ(rows.size(), 2);
    
    auto cells = doc->getElementsByTagName("td");
    EXPECT_EQ(cells.size(), 1);
    
    auto headers = doc->getElementsByTagName("th");
    EXPECT_EQ(headers.size(), 1);
}

TEST(TreeBuilder_FormElements) {
    std::string html = "<form action=\"/submit\" method=\"post\">"
                       "<input type=\"text\" name=\"username\" />"
                       "<input type=\"submit\" value=\"Submit\" />"
                       "</form>";
    HtmlTreeBuilder builder;
    auto doc = builder.build(html);
    
    auto forms = doc->getElementsByTagName("form");
    EXPECT_EQ(forms.size(), 1);
    
    auto form = forms[0];
    EXPECT_STREQ(form->getAttribute("action")->c_str(), "/submit");
    EXPECT_STREQ(form->getAttribute("method")->c_str(), "post");
    
    auto inputs = doc->getElementsByTagName("input");
    EXPECT_EQ(inputs.size(), 2);
}

TEST(TreeBuilder_ScriptStyle) {
    std::string html = "<head>"
                       "<style>body { color: red; }</style>"
                       "<script>console.log('test');</script>"
                       "</head>";
    HtmlTreeBuilder builder;
    auto doc = builder.build(html);
    
    auto styles = doc->getElementsByTagName("style");
    EXPECT_EQ(styles.size(), 1);
    EXPECT_TRUE(styles[0]->textContent().find("color: red") != std::string::npos);
    
    auto scripts = doc->getElementsByTagName("script");
    EXPECT_EQ(scripts.size(), 1);
}

TEST(Dom_ElementAttributes) {
    auto elem = std::make_shared<Element>("div");
    
    elem->setAttribute("id", "test");
    elem->setAttribute("class", "container main");
    
    EXPECT_TRUE(elem->hasAttribute("id"));
    EXPECT_STREQ(elem->getAttribute("id")->c_str(), "test");
    EXPECT_STREQ(elem->id().c_str(), "test");
    EXPECT_STREQ(elem->className().c_str(), "container main");
    
    elem->removeAttribute("id");
    EXPECT_FALSE(elem->hasAttribute("id"));
}

TEST(Dom_ElementClassList) {
    auto elem = std::make_shared<Element>("div");
    elem->setClassName("foo bar baz");
    
    auto classList = elem->classList();
    EXPECT_EQ(classList.size(), 3);
    EXPECT_TRUE(elem->hasClass("foo"));
    EXPECT_TRUE(elem->hasClass("bar"));
    EXPECT_TRUE(elem->hasClass("baz"));
    EXPECT_FALSE(elem->hasClass("qux"));
    
    elem->addClass("qux");
    EXPECT_TRUE(elem->hasClass("qux"));
    
    elem->removeClass("bar");
    EXPECT_FALSE(elem->hasClass("bar"));
}

TEST(Dom_ElementTraversal) {
    HtmlTreeBuilder builder;
    auto doc = builder.build(std::string("<div><p>First</p><p>Second</p><span>Third</span></div>"));
    
    auto divs = doc->getElementsByTagName("div");
    EXPECT_EQ(divs.size(), 1);
    
    auto div = divs[0];
    EXPECT_EQ(div->childElementCount(), 3);
    
    auto firstChild = div->firstElementChild();
    EXPECT_TRUE(firstChild != nullptr);
    EXPECT_STREQ(firstChild->localName().c_str(), "p");
    
    auto lastChild = div->lastElementChild();
    EXPECT_TRUE(lastChild != nullptr);
    EXPECT_STREQ(lastChild->localName().c_str(), "span");
    
    auto next = firstChild->nextElementSibling();
    EXPECT_TRUE(next != nullptr);
    EXPECT_STREQ(next->localName().c_str(), "p");
    
    auto prev = lastChild->previousElementSibling();
    EXPECT_TRUE(prev != nullptr);
    EXPECT_STREQ(prev->localName().c_str(), "p");
}

TEST(Dom_GetElementById) {
    HtmlTreeBuilder builder;
    auto doc = builder.build(std::string("<div id=\"outer\"><p id=\"inner\">Text</p></div>"));
    
    auto outer = doc->getElementById("outer");
    EXPECT_TRUE(outer != nullptr);
    EXPECT_STREQ(outer->localName().c_str(), "div");
    
    auto inner = doc->getElementById("inner");
    EXPECT_TRUE(inner != nullptr);
    EXPECT_STREQ(inner->localName().c_str(), "p");
}

TEST(Dom_GetElementsByTagName) {
    HtmlTreeBuilder builder;
    auto doc = builder.build(std::string("<div><p>A</p><p>B</p><p>C</p></div>"));
    
    auto paragraphs = doc->getElementsByTagName("p");
    EXPECT_EQ(paragraphs.size(), 3);
    
    auto all = doc->getElementsByTagName("*");
    EXPECT_GT(all.size(), 3);
}

TEST(Dom_GetElementsByClassName) {
    HtmlTreeBuilder builder;
    auto doc = builder.build(std::string("<div class=\"container\"><p class=\"text highlight\">A</p>"
                             "<span class=\"text\">B</span></div>"));
    
    auto texts = doc->getElementsByClassName("text");
    EXPECT_EQ(texts.size(), 2);
    
    auto highlights = doc->getElementsByClassName("highlight");
    EXPECT_EQ(highlights.size(), 1);
}

TEST(Dom_CloneNode) {
    HtmlTreeBuilder builder;
    auto doc = builder.build(std::string("<div id=\"original\"><p>Text</p></div>"));
    
    auto original = doc->getElementById("original");
    auto shallowClone = std::static_pointer_cast<Element>(original->cloneNode(false));
    
    EXPECT_STREQ(shallowClone->localName().c_str(), "div");
    EXPECT_STREQ(shallowClone->id().c_str(), "original");
    EXPECT_EQ(shallowClone->childElementCount(), 0);
    
    auto deepClone = std::static_pointer_cast<Element>(original->cloneNode(true));
    EXPECT_EQ(deepClone->childElementCount(), 1);
}

TEST(Dom_ToHtml) {
    HtmlTreeBuilder builder;
    auto doc = builder.build(std::string("<div id=\"test\"><p>Hello</p></div>"));
    
    std::string html = doc->toHtml();
    EXPECT_TRUE(html.find("<div") != std::string::npos);
    EXPECT_TRUE(html.find("id=\"test\"") != std::string::npos);
    EXPECT_TRUE(html.find("<p>") != std::string::npos);
    EXPECT_TRUE(html.find("Hello") != std::string::npos);
}

TEST(HtmlParser_ParseBasicDocument) {
    std::string html = "<!DOCTYPE html>"
                       "<html>"
                       "<head>"
                       "<meta charset=\"UTF-8\">"
                       "<title>Test Page</title>"
                       "</head>"
                       "<body>"
                       "<h1>Welcome</h1>"
                       "<p>This is a test.</p>"
                       "</body>"
                       "</html>";
    
    auto result = HtmlParser::parseHtml(html);
    
    EXPECT_TRUE(result.ok());
    EXPECT_TRUE(result.document != nullptr);
    EXPECT_STREQ(HtmlParser::extractTitle(result.document).c_str(), "Test Page");
}

TEST(HtmlParser_ExtractLinks) {
    std::string html = "<html><body>"
                       "<a href=\"/page1\">Link 1</a>"
                       "<a href=\"https://example.com/page2\">Link 2</a>"
                       "<img src=\"/image.png\" />"
                       "<script src=\"/script.js\"></script>"
                       "<link rel=\"stylesheet\" href=\"/style.css\" />"
                       "</body></html>";
    
    auto result = HtmlParser::parseHtml(html);
    auto links = HtmlParser::extractLinks(result.document);
    
    EXPECT_GE(links.size(), 4);
}

TEST(HtmlParser_ExtractScripts) {
    std::string html = "<html><head>"
                       "<script src=\"/app.js\"></script>"
                       "<script src=\"/vendor.js\"></script>"
                       "</head><body>"
                       "<script>inline script</script>"
                       "</body></html>";
    
    auto result = HtmlParser::parseHtml(html);
    auto scripts = HtmlParser::extractScripts(result.document);
    
    EXPECT_EQ(scripts.size(), 2);
}

TEST(HtmlParser_ExtractStylesheets) {
    std::string html = "<html><head>"
                       "<link rel=\"stylesheet\" href=\"/style.css\" />"
                       "<link rel=\"stylesheet\" href=\"/print.css\" media=\"print\" />"
                       "</head></html>";
    
    auto result = HtmlParser::parseHtml(html);
    auto stylesheets = HtmlParser::extractStylesheets(result.document);
    
    EXPECT_EQ(stylesheets.size(), 2);
}

TEST(HtmlParser_ExtractImages) {
    std::string html = "<html><body>"
                       "<img src=\"/img1.png\" />"
                       "<img src=\"/img2.jpg\" alt=\"Image 2\" />"
                       "<picture><source srcset=\"/img3.webp\" /><img src=\"/img3.jpg\" /></picture>"
                       "</body></html>";
    
    auto result = HtmlParser::parseHtml(html);
    auto images = HtmlParser::extractImages(result.document);
    
    EXPECT_GE(images.size(), 2);
}

TEST(HtmlParser_ExtractMetaTags) {
    std::string html = "<html><head>"
                       "<meta name=\"description\" content=\"Test page\">"
                       "<meta name=\"keywords\" content=\"test, html, parser\">"
                       "<meta property=\"og:title\" content=\"Open Graph Title\">"
                       "</head></html>";
    
    auto result = HtmlParser::parseHtml(html);
    auto metaTags = HtmlParser::extractMetaTags(result.document);
    
    EXPECT_EQ(metaTags["description"], "Test page");
    EXPECT_EQ(metaTags["keywords"], "test, html, parser");
    EXPECT_EQ(metaTags["og:title"], "Open Graph Title");
}

TEST(HtmlParser_MalformedHtml) {
    std::string html = "<div><p>Unclosed paragraph"
                       "<span>Nested<span>"
                       "</div>";
    
    auto result = HtmlParser::parseHtml(html);
    
    EXPECT_TRUE(result.ok());
    EXPECT_TRUE(result.document != nullptr);
}

TEST(HtmlParser_VoidElements) {
    std::string html = "<div>"
                       "<br>"
                       "<hr>"
                       "<img src=\"test.png\">"
                       "<input type=\"text\">"
                       "<meta charset=\"UTF-8\">"
                       "</div>";
    
    auto result = HtmlParser::parseHtml(html);
    EXPECT_TRUE(result.ok());
    
    auto divs = result.document->getElementsByTagName("div");
    EXPECT_EQ(divs.size(), 1);
    
    auto brs = result.document->getElementsByTagName("br");
    EXPECT_EQ(brs.size(), 1);
    
    auto hrs = result.document->getElementsByTagName("hr");
    EXPECT_EQ(hrs.size(), 1);
}

TEST(HtmlParser_EntityDecoding) {
    std::string html = "<div>&amp;&lt;&gt;&quot;&apos;</div>";

    auto result = HtmlParser::parseHtml(html);
    EXPECT_TRUE(result.ok());

    auto divs = result.document->getElementsByTagName("div");
    EXPECT_EQ(divs.size(), 1);

    std::string content = divs[0]->textContent();
    EXPECT_TRUE(content.find("&") != std::string::npos);
    EXPECT_TRUE(content.find("<") != std::string::npos);
    EXPECT_TRUE(content.find(">") != std::string::npos);
}

// ── Extended named entities ──────────────────────────────────

TEST(HtmlParser_NamedEntities_Extended) {
    std::string html = "<p>&mdash;&eacute;&frac12;&Sigma;&larr;&euro;&hellip;</p>";

    auto result = HtmlParser::parseHtml(html);
    EXPECT_TRUE(result.ok());

    auto ps = result.document->getElementsByTagName("p");
    ASSERT_EQ(ps.size(), (size_t)1);
    std::string content = ps[0]->textContent();

    EXPECT_TRUE(content.find("\u2014") != std::string::npos); // mdash
    EXPECT_TRUE(content.find("\u00E9") != std::string::npos); // eacute
    EXPECT_TRUE(content.find("\u00BD") != std::string::npos); // frac12
    EXPECT_TRUE(content.find("\u03A3") != std::string::npos); // Sigma
    EXPECT_TRUE(content.find("\u2190") != std::string::npos); // larr
    EXPECT_TRUE(content.find("\u20AC") != std::string::npos); // euro
    EXPECT_TRUE(content.find("\u2026") != std::string::npos); // hellip
}

TEST(HtmlParser_EntityLongestMatch) {
    // "&notin" must win over "&not" when followed by "in"
    {
        auto result = HtmlParser::parseHtml("<p>x&notin;y</p>");
        auto ps = result.document->getElementsByTagName("p");
        ASSERT_EQ(ps.size(), (size_t)1);
        std::string content = ps[0]->textContent();
        EXPECT_TRUE(content.find("\u2209") != std::string::npos); // ∉
        EXPECT_TRUE(content.find("x") != std::string::npos);
        EXPECT_TRUE(content.find("y") != std::string::npos);
    }
    {
        // "&amp" matched without semicolon leaves the rest as text
        auto result = HtmlParser::parseHtml("<p>u&ampx;</p>");
        auto ps = result.document->getElementsByTagName("p");
        ASSERT_EQ(ps.size(), (size_t)1);
        EXPECT_STREQ(ps[0]->textContent().c_str(), "u&x;");
    }
    {
        // "b" right after "&amp" is not part of the entity
        auto result = HtmlParser::parseHtml("<p>a&ampb</p>");
        auto ps = result.document->getElementsByTagName("p");
        ASSERT_EQ(ps.size(), (size_t)1);
        EXPECT_STREQ(ps[0]->textContent().c_str(), "a&b");
    }
}

TEST(HtmlParser_EntityWithoutSemicolon_Legacy) {
    auto result = HtmlParser::parseHtml("<p>&copy 2024</p>");
    auto ps = result.document->getElementsByTagName("p");
    ASSERT_EQ(ps.size(), (size_t)1);
    std::string content = ps[0]->textContent();
    EXPECT_TRUE(content.find("\u00A9") != std::string::npos); // ©
    EXPECT_TRUE(content.find("2024") != std::string::npos);
}

TEST(HtmlParser_NumericC1Remap) {
    // &#151; maps to em dash via the windows-1252 table, same as &#8212;
    {
        auto result = HtmlParser::parseHtml("<p>&#8212;</p>");
        auto ps = result.document->getElementsByTagName("p");
        ASSERT_EQ(ps.size(), (size_t)1);
        EXPECT_TRUE(ps[0]->textContent() == std::string("\xE2\x80\x94"));
    }
    {
        auto result = HtmlParser::parseHtml("<p>&#151;</p>");
        auto ps = result.document->getElementsByTagName("p");
        ASSERT_EQ(ps.size(), (size_t)1);
        EXPECT_TRUE(ps[0]->textContent() == std::string("\xE2\x80\x94"));
    }
    {
        auto result = HtmlParser::parseHtml("<p>&#146;</p>");
        auto ps = result.document->getElementsByTagName("p");
        ASSERT_EQ(ps.size(), (size_t)1);
        EXPECT_TRUE(ps[0]->textContent() == std::string("\xE2\x80\x99"));
    }
}

// ── Fragment parsing with context element ────────────────────

TEST(FragmentParsing_TableContext) {
    auto doc = std::make_shared<Document>();
    auto tr = doc->createElement("tr");

    // In body context a <td> start tag is an error and gets dropped; in a
    // tr context it must parse as a table cell.
    auto nodes = HtmlParser::parseFragment("<td>a</td><td>b</td>", tr);
    ASSERT_EQ(nodes.size(), (size_t)2);
    EXPECT_TRUE(nodes[0]->nodeType() == NodeType::Element);
    auto td1 = std::static_pointer_cast<Element>(nodes[0]);
    auto td2 = std::static_pointer_cast<Element>(nodes[1]);
    EXPECT_STREQ(td1->localName().c_str(), "td");
    EXPECT_STREQ(td2->localName().c_str(), "td");
    EXPECT_STREQ(td1->textContent().c_str(), "a");
    EXPECT_STREQ(td2->textContent().c_str(), "b");
}

TEST(FragmentParsing_TextInCellContext) {
    auto doc = std::make_shared<Document>();
    auto td = doc->createElement("td");

    auto nodes = HtmlParser::parseFragment("hello <b>world</b>", td);
    ASSERT_EQ(nodes.size(), (size_t)2);
    EXPECT_TRUE(nodes[0]->nodeType() == NodeType::Text);
    EXPECT_TRUE(nodes[1]->nodeType() == NodeType::Element);
    EXPECT_STREQ(std::static_pointer_cast<Element>(nodes[1])->localName().c_str(),
                 "b");
}

TEST(FragmentParsing_DefaultBodyContextUnchanged) {
    auto nodes = HtmlParser::parseFragment("<div>x</div>");
    ASSERT_EQ(nodes.size(), (size_t)1);
    EXPECT_TRUE(nodes[0]->nodeType() == NodeType::Element);
    EXPECT_STREQ(std::static_pointer_cast<Element>(nodes[0])->localName().c_str(),
                 "div");
}

TEST(FragmentParsing_InsertAdjacentTd) {
    auto doc = std::make_shared<Document>();
    auto table = doc->createElement("table");
    auto tr = doc->createElement("tr");
    auto td = doc->createElement("td");
    td->appendChild(doc->createTextNode("first"));
    tr->appendChild(td);
    table->appendChild(tr);
    doc->appendChild(table);

    td->insertAdjacentHTML("afterend", "<td>next</td>");
    ASSERT_EQ(tr->childElementCount(), (size_t)2);
    EXPECT_STREQ(tr->lastElementChild()->textContent().c_str(), "next");
}
