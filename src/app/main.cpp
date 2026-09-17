#include <memory>

#include <wx/wx.h>

#include "src/component/component_factory.hpp"
#include "src/component/component_library.hpp"
#include "src/editor/editor_controller.hpp"
#include "src/model/schematic_model.hpp"
#include "src/ui/main_frame.hpp"

namespace cse::app {

class CircuitSchematicEditorApp final : public wxApp {
public:
    bool OnInit() override {
        editor_ = std::make_unique<editor::EditorController>(model_, library_, factory_);
        auto* frame = new ui::MainFrame(*editor_, library_);
        SetTopWindow(frame);
        frame->Show(true);
        return true;
    }

private:
    model::SchematicModel model_;
    component::ComponentLibrary library_;
    component::ComponentFactory factory_{library_};
    std::unique_ptr<editor::EditorController> editor_;
};

}  // namespace cse::app

wxIMPLEMENT_APP(cse::app::CircuitSchematicEditorApp);
