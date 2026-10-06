#include <engine/browsing_context.hpp>

namespace xiaopeng {
namespace engine {

void BrowsingContext::pushState(const std::string &state,
                                 const std::string &title,
                                 const std::string &url_str) {
  HistoryEntry entry{url_str, title, state, ""};

  // If current index is not last, truncate history from index+1
  if (m_currentIndex >= 0 && m_currentIndex < (int)m_history.size() - 1) {
    m_history.erase(m_history.begin() + m_currentIndex + 1, m_history.end());
  }

  // Add new entry
  m_history.push_back(entry);
  m_currentIndex = (int)m_history.size() - 1;
}

void BrowsingContext::replaceState(const std::string &state,
                                    const std::string &title,
                                    const std::string &url_str) {
  if (m_currentIndex < 0 || m_currentIndex >= (int)m_history.size()) {
    return;
  }

  m_history[m_currentIndex] = HistoryEntry{url_str, title, state, ""};
}

void BrowsingContext::go(int delta) {
  if (delta == 0) {
    return;
  }

  int new_index = m_currentIndex + delta;

  // Bounds check
  if (new_index < 0 || new_index >= (int)m_history.size()) {
    return;
  }

  m_currentIndex = new_index;

  // Fire popstate event
  firePopStateEvent(getState());
}

void BrowsingContext::back() { go(-1); }

void BrowsingContext::forward() { go(1); }

std::string BrowsingContext::getState() const {
  if (m_currentIndex < 0 || m_currentIndex >= (int)m_history.size()) {
    return "";
  }
  return m_history[m_currentIndex].state;
}

std::string BrowsingContext::getUrl() const {
  if (m_currentIndex < 0 || m_currentIndex >= (int)m_history.size()) {
    return "";
  }
  return m_history[m_currentIndex].url;
}

void BrowsingContext::setEventTarget(std::shared_ptr<void> target) {
  m_eventTarget = target;
}

void BrowsingContext::navigate(const std::string &url) {
  // Create new entry
  HistoryEntry entry{url, "", "", ""};

  // If current index is not last, truncate history from index+1
  if (m_currentIndex >= 0 && m_currentIndex < (int)m_history.size() - 1) {
    m_history.erase(m_history.begin() + m_currentIndex + 1, m_history.end());
  }

  m_history.push_back(entry);
  m_currentIndex = (int)m_history.size() - 1;

  // Fire popstate event with empty state
  firePopStateEvent("");
}

void BrowsingContext::firePopStateEvent(const std::string &) {
  // Currently, just a placeholder.
  // In a real implementation, we'd dispatch an event to m_eventTarget.
}

void BrowsingContext::clear() {
  m_history.clear();
  m_currentIndex = -1;
}

} // namespace engine
} // namespace xiaopeng
