#pragma once

#include "../loader/error.hpp"
#include "../loader/resource.hpp"
#include "dom.hpp"
#include "html_tokenizer.hpp"
#include "html_tree_builder.hpp"
#include <functional>

namespace xiaopeng {
namespace dom {

struct ParserOptions {
  bool scripting = false;
  bool allowCdata = false;
  bool strictMode = false;
  std::string encoding = "UTF-8";
  std::string baseUrl;
};

struct ParserResult {
  std::shared_ptr<Document> document;
  std::vector<ParseError> errors;
  bool hasErrors = false;
  std::string quirksMode;

  bool ok() const { return document != nullptr; }
};

class HtmlParser {
public:
  using ErrorCallback = std::function<void(const ParseError &)>;

  HtmlParser() = default;
  explicit HtmlParser(const ParserOptions &options) : options_(options) {}

  ParserResult parse(const std::string &html) { return parseInternal(html); }

  ParserResult parse(const loader::ByteBuffer &html) {
    return parseInternal(html);
  }

  ParserResult parse(std::string_view html) { return parseInternal(html); }

  ParserResult parse(const loader::Resource &resource) {
    ParserOptions opts = options_;
    if (!resource.charset.empty()) {
      opts.encoding = resource.charset;
    }

    ParserResult result = parseInternal(resource.data);

    if (result.document) {
      result.document->setUrl(resource.url.toString());
      result.document->setContentType(
          resource.mimeType.empty() ? "text/html" : resource.mimeType);
      result.document->setCharacterSet(opts.encoding);
    }

    return result;
  }

  static ParserResult parseHtml(const std::string &html,
                                const ParserOptions &options = {}) {
    HtmlParser parser(options);
    return parser.parse(html);
  }

  static ParserResult parseHtml(const loader::ByteBuffer &html,
                                const ParserOptions &options = {}) {
    HtmlParser parser(options);
    return parser.parse(html);
  }

  static ParserResult parseHtml(const loader::Resource &resource,
                                const ParserOptions &options = {}) {
    HtmlParser parser(options);
    return parser.parse(resource);
  }

  static std::vector<std::shared_ptr<Node>>
  parseFragment(const std::string &html, const ParserOptions &options = {}) {
    HtmlParser parser(options);
    auto result = parser.parse(html);
    if (result.document) {
      std::vector<std::shared_ptr<Node>> nodes;
      auto body = result.document->body();
      if (body) {
        // Return a copy of children
        // We need to be careful: these nodes are attached to a temporary
        // document. In a real browser, we'd reparent them. For now, simple copy
        // is fine as shared_ptr keeps them alive.
        for (auto &child : body->childNodes()) {
          nodes.push_back(child);
        }
      }
      return nodes;
    }
    return {};
  }

  // Fragment parsing relative to a context element (WHATWG fragment case).
  // The context element determines the insertion mode, so fragments like
  // "<td>...</td>" parse correctly against a tr context.
  static std::vector<std::shared_ptr<Node>>
  parseFragment(const std::string &html,
                const std::shared_ptr<Element> &context,
                const ParserOptions &options = {}) {
    (void)options;
    HtmlTreeBuilder builder;
    return builder.buildFragment(html, context);
  }

  void setOptions(const ParserOptions &options) { options_ = options; }
  const ParserOptions &options() const { return options_; }

  void setErrorCallback(ErrorCallback callback) {
    errorCallback_ = std::move(callback);
  }

  static std::vector<std::string>
  extractLinks(const std::shared_ptr<Document> &document) {
    std::vector<std::string> links;

    if (!document)
      return links;

    extractLinksFromElement(document->documentElement(), links);

    return links;
  }

  struct ScriptData {
    std::string src;
    std::string content;
    bool async = false;
    bool defer = false;
  };

  static std::vector<ScriptData>
  extractScriptData(const std::shared_ptr<Document> &document) {
    std::vector<ScriptData> scripts;

    if (!document)
      return scripts;

    auto scriptElements = document->getElementsByTagName("script");
    for (const auto &script : scriptElements) {
      ScriptData data;
      auto src = script->getAttribute("src");
      if (src && !src->empty()) {
        data.src = *src;
      }
      data.content = script->textContent();
      // Read async/defer boolean attributes
      auto asyncAttr = script->getAttribute("async");
      data.async = asyncAttr.has_value();
      auto deferAttr = script->getAttribute("defer");
      data.defer = deferAttr.has_value();
      scripts.push_back(data);
    }

    return scripts;
  }

  static std::vector<std::string>
  extractScripts(const std::shared_ptr<Document> &document) {
    std::vector<std::string> scripts;
    auto scriptData = extractScriptData(document);
    for (const auto &script : scriptData) {
      if (!script.src.empty()) {
        scripts.push_back(script.src);
      }
    }
    return scripts;
  }

  static std::vector<std::string>
  extractStylesheets(const std::shared_ptr<Document> &document) {
    std::vector<std::string> stylesheets;

    if (!document)
      return stylesheets;

    auto linkElements = document->getElementsByTagName("link");
    for (const auto &link : linkElements) {
      auto rel = link->getAttribute("rel");
      if (rel && toLower(*rel) == "stylesheet") {
        auto href = link->getAttribute("href");
        if (href && !href->empty()) {
          stylesheets.push_back(*href);
        }
      }
    }

    auto styleElements = document->getElementsByTagName("style");
    for (const auto &style : styleElements) {
      auto importPos = style->textContent().find("@import");
      if (importPos != std::string::npos) {
        std::string content = style->textContent();
        size_t pos = importPos;
        while ((pos = content.find("@import", pos)) != std::string::npos) {
          pos += 7;
          while (pos < content.size() && isWhitespace(content[pos]))
            pos++;

          if (pos < content.size()) {
            char quote = '\0';
            if (content[pos] == '"' || content[pos] == '\'') {
              quote = content[pos];
              pos++;
            }

            size_t start = pos;
            while (pos < content.size()) {
              if (quote && content[pos] == quote)
                break;
              if (!quote && (content[pos] == ';' || isWhitespace(content[pos])))
                break;
              pos++;
            }

            std::string url = content.substr(start, pos - start);
            url = trimWhitespace(url);
            if (!url.empty()) {
              stylesheets.push_back(url);
            }
          }
        }
      }
    }

    return stylesheets;
  }

  static std::vector<std::string>
  extractImages(const std::shared_ptr<Document> &document) {
    std::vector<std::string> images;

    if (!document)
      return images;

    auto imgElements = document->getElementsByTagName("img");
    for (const auto &img : imgElements) {
      auto src = img->getAttribute("src");
      if (src && !src->empty()) {
        images.push_back(*src);
      }
    }

    return images;
  }

  static std::vector<std::string>
  extractFavicons(const std::shared_ptr<Document> &document) {
    std::vector<std::string> favicons;

    if (!document)
      return favicons;

    auto linkElements = document->getElementsByTagName("link");
    for (const auto &link : linkElements) {
      auto rel = link->getAttribute("rel");
      if (rel) {
        std::string relLower = toLower(*rel);
        if (relLower == "icon" || relLower == "shortcut icon" ||
            relLower == "apple-touch-icon") {
          auto href = link->getAttribute("href");
          if (href && !href->empty()) {
            favicons.push_back(*href);
          }
        }
      }
    }

    return favicons;
  }

  static std::string extractTitle(const std::shared_ptr<Document> &document) {
    if (!document)
      return "";
    return document->titleText();
  }

  static std::string
  extractMetaDescription(const std::shared_ptr<Document> &document) {
    if (!document)
      return "";

    auto metaElements = document->getElementsByTagName("meta");
    for (const auto &meta : metaElements) {
      auto name = meta->getAttribute("name");
      if (name && toLower(*name) == "description") {
        auto content = meta->getAttribute("content");
        return content.value_or("");
      }
    }

    return "";
  }

  static std::unordered_map<std::string, std::string>
  extractMetaTags(const std::shared_ptr<Document> &document) {
    std::unordered_map<std::string, std::string> metaTags;

    if (!document)
      return metaTags;

    auto metaElements = document->getElementsByTagName("meta");
    for (const auto &meta : metaElements) {
      auto name = meta->getAttribute("name");
      auto property = meta->getAttribute("property");
      auto content = meta->getAttribute("content");

      if (content) {
        if (name && !name->empty()) {
          metaTags[toLower(*name)] = *content;
        } else if (property && !property->empty()) {
          metaTags[toLower(*property)] = *content;
        }
      }
    }

    return metaTags;
  }

  static std::string
  extractBaseHref(const std::shared_ptr<Document> &document) {
    if (!document)
      return "";

    auto baseElements = document->getElementsByTagName("base");
    for (const auto &base : baseElements) {
      auto href = base->getAttribute("href");
      if (href && !href->empty()) {
        return *href;
      }
    }

    return "";
  }

private:
  template <typename T> ParserResult parseInternal(const T &html) {
    ParserResult result;

    HtmlTokenizer tokenizer(html);
    tokenizer.setAllowCdata(options_.allowCdata);

    if (errorCallback_) {
      tokenizer.setErrorCallback(errorCallback_);
    }

    std::vector<Token> tokens = tokenizer.tokenize();

    HtmlTreeBuilder builder;
    builder.setScriptingFlag(options_.scripting);

    if (errorCallback_) {
      builder.setErrorCallback(errorCallback_);
    }

    result.document = builder.build(tokens);
    result.errors = builder.errors();
    result.hasErrors = !result.errors.empty();
    result.quirksMode = builder.quirksMode() ? "BackCompat" : "CSS1Compat";

    if (result.document) {
      result.document->setCharacterSet(options_.encoding);
      if (!options_.baseUrl.empty()) {
        result.document->setUrl(options_.baseUrl);
      }
    }

    return result;
  }

  static void extractLinksFromElement(const ElementPtr &element,
                                      std::vector<std::string> &links) {
    if (!element)
      return;

    std::string tagName = toLower(element->localName());

    if (tagName == "a" || tagName == "area" || tagName == "link") {
      auto href = element->getAttribute("href");
      if (href && !href->empty() && href->find("javascript:") != 0 &&
          href->find("data:") != 0 && href->find("mailto:") != 0 &&
          href->find("#") != 0) {
        links.push_back(*href);
      }
    }

    if (tagName == "img" || tagName == "script" || tagName == "iframe" ||
        tagName == "embed" || tagName == "video" || tagName == "audio" ||
        tagName == "source" || tagName == "track") {
      auto src = element->getAttribute("src");
      if (src && !src->empty() && src->find("data:") != 0) {
        links.push_back(*src);
      }
    }

    if (tagName == "img") {
      auto srcset = element->getAttribute("srcset");
      if (srcset && !srcset->empty()) {
        extractSrcsetLinks(*srcset, links);
      }
    }

    if (tagName == "picture") {
      auto sourceElements = element->getElementsByTagName("source");
      for (const auto &source : sourceElements) {
        auto srcset = source->getAttribute("srcset");
        if (srcset && !srcset->empty()) {
          extractSrcsetLinks(*srcset, links);
        }
      }
    }

    if (tagName == "object") {
      auto data = element->getAttribute("data");
      if (data && !data->empty() && data->find("data:") != 0) {
        links.push_back(*data);
      }
    }

    if (tagName == "form") {
      auto action = element->getAttribute("action");
      if (action && !action->empty()) {
        links.push_back(*action);
      }
    }

    for (const auto &child : element->childNodes()) {
      if (child->nodeType() == NodeType::Element) {
        extractLinksFromElement(std::static_pointer_cast<Element>(child),
                                links);
      }
    }
  }

  static void extractSrcsetLinks(const std::string &srcset,
                                 std::vector<std::string> &links) {
    size_t pos = 0;
    while (pos < srcset.size()) {
      while (pos < srcset.size() && isWhitespace(srcset[pos]))
        pos++;

      size_t start = pos;
      while (pos < srcset.size() && !isWhitespace(srcset[pos]))
        pos++;

      if (start < pos) {
        std::string url = srcset.substr(start, pos - start);
        if (!url.empty() && url.find("data:") != 0) {
          links.push_back(url);
        }
      }

      while (pos < srcset.size() && isWhitespace(srcset[pos]))
        pos++;

      while (pos < srcset.size() && !isWhitespace(srcset[pos]))
        pos++;
    }
  }

  ParserOptions options_;
  ErrorCallback errorCallback_;
};

} // namespace dom
} // namespace xiaopeng
