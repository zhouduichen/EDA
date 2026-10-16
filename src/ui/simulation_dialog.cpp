#include "src/ui/simulation_dialog.hpp"

#include <unordered_map>

#include <wx/checkbox.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

#include "src/model/net_builder.hpp"
#include "src/sim/logic_simulator.hpp"
#include "src/ui/schematic_canvas.hpp"

namespace cse::ui {

namespace {

    wxString ValueText(LogicValue value) {
        switch (value) {
            case LogicValue::kHigh:
                return L"1";
            case LogicValue::kLow:
                return L"0";
            case LogicValue::kUnknown:
                return L"X";
            case LogicValue::kHighImpedance:
                return L"Z";
        }
        return L"?";
    }

}  // namespace

SimulationDialog::SimulationDialog(wxWindow* parent, editor::EditorController& editor,
                                   const IComponentLibrary& library, SchematicCanvas* canvas,
                                   std::function<void()> on_closed)
    : wxDialog(parent, wxID_ANY, L"逻辑仿真", wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      editor_(editor),
      library_(library),
      canvas_(canvas),
      on_closed_(std::move(on_closed)) {
    BuildContent();
    Bind(wxEVT_CLOSE_WINDOW, &SimulationDialog::OnClose, this);
    Run();
    Show(true);
}

void SimulationDialog::OnClose(wxCloseEvent& event) {
    static_cast<void>(event);
    if (canvas_ != nullptr) {
        canvas_->SetSimulationResult(std::nullopt);
    }
    if (on_closed_) {
        on_closed_();
    }
    Destroy();
}

wxString SimulationDialog::ComponentLabel(const ComponentInstance& component) const {
    const auto label_iterator = component.properties.find("label");
    if (label_iterator != component.properties.end() && !label_iterator->second.empty()) {
        return wxString(component.reference) + L"（" + wxString(label_iterator->second) + L"）";
    }
    return wxString(component.reference);
}

void SimulationDialog::BuildContent() {
    root_sizer_ = new wxBoxSizer(wxVERTICAL);

    root_sizer_->Add(new wxStaticText(this, wxID_ANY, L"输入（勾选 = 高电平）"), 0,
                    wxALL, 8);
    const SchematicSnapshot snapshot = editor_.GetSnapshot();
    bool has_input = false;
    for (const ComponentInstance& component : snapshot.components) {
        if (component.type_id != "io.input") {
            continue;
        }
        has_input = true;
        auto* check = new wxCheckBox(this, wxID_ANY, ComponentLabel(component));
        root_sizer_->Add(check, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
        check->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { Run(); });
        input_checks_.emplace_back(component.id, check);
    }
    if (!has_input) {
        root_sizer_->Add(new wxStaticText(this, wxID_ANY, L"（图中没有 io.input 元件）"), 0,
                        wxLEFT | wxRIGHT | wxBOTTOM, 8);
    }

    root_sizer_->Add(new wxStaticText(this, wxID_ANY, L"输出"), 0, wxALL, 8);
    bool has_output = false;
    for (const ComponentInstance& component : snapshot.components) {
        if (component.type_id != "io.output") {
            continue;
        }
        has_output = true;
        auto* value_text = new wxStaticText(this, wxID_ANY, ComponentLabel(component) + L" = ?");
        root_sizer_->Add(value_text, 0, wxLEFT | wxRIGHT | wxBOTTOM, 8);
        output_values_.emplace_back(component.id, value_text);
    }
    if (!has_output) {
        root_sizer_->Add(new wxStaticText(this, wxID_ANY, L"（图中没有 io.output 元件）"), 0,
                        wxLEFT | wxRIGHT | wxBOTTOM, 8);
    }

    status_text_ = new wxStaticText(this, wxID_ANY, wxEmptyString);
    root_sizer_->Add(status_text_, 0, wxALL, 8);

    root_sizer_->Add(new wxStaticText(this, wxID_ANY,
                                      L"画布可视化：导线颜色 红=1 绿=0 橙=X（未知）"),
                    0, wxALL, 8);

    SetSizerAndFit(root_sizer_);
}

void SimulationDialog::RefreshCircuit() {
    std::unordered_map<ComponentId, bool> input_states;
    for (const auto& [component_id, check] : input_checks_) {
        input_states[component_id] = check->GetValue();
    }

    input_checks_.clear();
    output_values_.clear();
    status_text_ = nullptr;
    wxSizer* old_sizer = GetSizer();
    if (old_sizer != nullptr) {
        old_sizer->Clear(true);
        SetSizer(nullptr, false);
        delete old_sizer;
    }
    root_sizer_ = nullptr;

    BuildContent();
    for (const auto& [component_id, check] : input_checks_) {
        const auto state_iterator = input_states.find(component_id);
        if (state_iterator != input_states.end()) {
            check->SetValue(state_iterator->second);
        }
    }
    Run();
}

void SimulationDialog::Run() {
    std::unordered_map<ComponentId, LogicValue> input_states;
    for (const auto& [component_id, check] : input_checks_) {
        input_states[component_id] = check->GetValue() ? LogicValue::kHigh : LogicValue::kLow;
    }

    const cse::sim::SimulationResult result = cse::sim::RunSimulation(editor_.GetSnapshot(),
                                                                      library_, input_states);
    if (canvas_ != nullptr) {
        canvas_->SetSimulationResult(result);
    }
    const SchematicSnapshot snapshot = editor_.GetSnapshot();

    // io.output 元件共用同一套引脚定义，输入引脚号查一次库就够，不写死 1。
    PinId output_input_pin = 1;
    const std::optional<ComponentDefinition> output_definition =
        library_.FindByTypeId("io.output");
    if (output_definition.has_value()) {
        for (const PinDefinition& pin : output_definition->pins) {
            if (pin.direction == PinDirection::kInput) {
                output_input_pin = pin.local_id;
                break;
            }
        }
    }

    for (const auto& [component_id, value_text] : output_values_) {
        LogicValue value = LogicValue::kUnknown;
        const auto value_iterator = result.pin_values.find(PinKey(component_id,
                                                                  output_input_pin));
        if (value_iterator != result.pin_values.end()) {
            value = value_iterator->second;
        }
        wxString label = L"?";
        for (const ComponentInstance& instance : snapshot.components) {
            if (instance.id == component_id) {
                label = ComponentLabel(instance);
                break;
            }
        }
        value_text->SetLabel(label + L" = " + ValueText(value));
    }

    if (result.stable) {
        status_text_->SetLabel(wxString::Format(L"稳定（传播 %d 次）", result.iterations));
    } else {
        status_text_->SetLabel(wxString::Format(L"未稳定（可能存在振荡，传播 %d 次）",
                                                result.iterations));
    }
}

}  // namespace cse::ui
