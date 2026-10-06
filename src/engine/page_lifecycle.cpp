#include <engine/page_lifecycle.hpp>

namespace xiaopeng {
namespace engine {

void PageLifecycleManager::setState(PageLifecycleState state) {
  m_state = state;
}

void PageLifecycleManager::fireDOMContentLoaded() {
  m_state = PageLifecycleState::Interactive;
  fireEvent(PageLifecycleEvent::DOMContentLoaded);
}

void PageLifecycleManager::fireLoad() {
  m_state = PageLifecycleState::Complete;
  fireEvent(PageLifecycleEvent::Load);
}

void PageLifecycleManager::fireBeforeUnload() {
  m_state = PageLifecycleState::Unloading;
  fireEvent(PageLifecycleEvent::BeforeUnload);
}

void PageLifecycleManager::fireUnload() {
  fireEvent(PageLifecycleEvent::Unload);
}

void PageLifecycleManager::addEventListener(PageLifecycleEvent event,
                                            EventCallback callback) {
  m_listeners[event].push_back(callback);
}

void PageLifecycleManager::removeEventListener(PageLifecycleEvent event,
                                               EventCallback) {
  // Note: This is a simplified implementation that doesn't handle
  // exact callback matching (since std::function isn't comparable)
  auto it = m_listeners.find(event);
  if (it != m_listeners.end()) {
    // For now, just clear all listeners for this event
    it->second.clear();
  }
}

void PageLifecycleManager::reset() {
  m_state = PageLifecycleState::Loading;
  m_listeners.clear();
}

void PageLifecycleManager::fireEvent(PageLifecycleEvent event) {
  auto it = m_listeners.find(event);
  if (it != m_listeners.end()) {
    // Make a copy to avoid issues if callbacks modify the listeners
    auto callbacks = it->second;
    for (auto &callback : callbacks) {
      callback();
    }
  }
}

} // namespace engine
} // namespace xiaopeng
