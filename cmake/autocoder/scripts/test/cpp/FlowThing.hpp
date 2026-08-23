#ifndef FLOW_THING_HPP
#define FLOW_THING_HPP
#include "FlowThingComponentBaseStub.hpp"

namespace RegTest {

class FlowThing : public FlowThingComponentBase {
  protected:
    void gIn_handler(int portNum) override;
    void sIn_handler(int portNum) override;
};

}  // namespace RegTest
#endif
