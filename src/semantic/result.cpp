#include "rx/semantic/safety/safe_gate.hpp"

namespace rx {

SemanticResult::SemanticResult()
    : data_(std::make_shared<const SemanticData>()) {}
SemanticResult SemanticResult::publish(const ast::Crate &crate,
                                       SemanticData data) {
  data.source_crate = &crate;
  data.success = true;
  auto snapshot = std::make_shared<const SemanticData>(std::move(data));
  SafeGate validated(crate, snapshot, SafeGate::ValidationTag{});
  return SemanticResult(std::move(snapshot));
}
SemanticResult SemanticResult::rejected(const ast::Crate &crate,
                                        SemanticData data) {
  data.source_crate = &crate;
  data.success = false;
  return SemanticResult(std::make_shared<const SemanticData>(std::move(data)));
}

} // namespace rx
