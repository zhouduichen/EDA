#pragma once

#include <functional>
#include <utility>
#include <vector>

#include <wx/dialog.h>

#include "contracts/ids.hpp"
#include "src/editor/editor_controller.hpp"

class wxCheckBox;
class wxCloseEvent;
class wxBoxSizer;
class wxStaticText;

namespace cse {
class IComponentLibrary;
}

namespace cse::ui {

class SchematicCanvas;

// 逻辑仿真对话框（非模态）：列出图上所有 io.input（勾选 = 高电平）和
// io.output，勾选变化立即重新传播；输出行实时刷新，同时把仿真结果推给
// 画布做可视化（导线按电平着色、IO 元件显示电平徽标）。
// 关闭时销毁自己并回调 on_closed，让主窗口清掉指针、画布恢复普通显示。
class SimulationDialog final : public wxDialog {
public:
    SimulationDialog(wxWindow* parent, editor::EditorController& editor,
                     const IComponentLibrary& library, SchematicCanvas* canvas,
                     std::function<void()> on_closed);

    // 电路模型变化后重建输入/输出列表，并保留仍存在的输入状态。
    void RefreshCircuit();

private:
    void BuildContent();
    void Run();
    void OnClose(wxCloseEvent& event);

    wxString ComponentLabel(const ComponentInstance& component) const;

    editor::EditorController& editor_;
    const IComponentLibrary& library_;
    SchematicCanvas* canvas_;
    std::function<void()> on_closed_;
    wxBoxSizer* root_sizer_{nullptr};
    std::vector<std::pair<ComponentId, wxCheckBox*>> input_checks_;
    std::vector<std::pair<ComponentId, wxStaticText*>> output_values_;
    wxStaticText* status_text_{nullptr};
};

}  // namespace cse::ui
