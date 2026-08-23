// Named ...BaseStub rather than ...Ac because the repository ignores *Ac.*
// as autocode. The extractor keys on the ComponentBase class name, not the file.
#ifndef FLOW_THING_AC_HPP
#define FLOW_THING_AC_HPP

namespace RegTest {

class FlowThingComponentBase {
  public:
    struct OutputPort {
        void invoke();
    };

  protected:
    virtual void gIn_handler(int portNum) = 0;
    virtual void sIn_handler(int portNum) = 0;

    void outX_out(int portNum);
    void outY_out(int portNum);

    OutputPort m_outX_OutputPort[1];
    OutputPort m_outY_OutputPort[1];
};

}  // namespace RegTest
#endif
