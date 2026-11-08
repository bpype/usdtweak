#include "ManipulatorEditability.h"
#include "Gui.h"
#include "Constants.h"
#include "UsdHelpers.h"
#include "Manipulator.h"
#include <pxr/usd/usdGeom/xformCommonAPI.h>

/// The attribute the gizmo writes on the prim, invalid when the gizmo can't edit it.
/// Through UsdGeomXformCommonAPI, a missing op is created and added to xformOpOrder, so xformOpOrder is what matters then.
static UsdAttribute GetEditedAttribute(const UsdGeomXformable &xformable, XformOpTypePredicate isEditedOpType) {
    bool resetsXformStack = false;
    const std::vector<UsdGeomXformOp> ops = xformable.GetOrderedXformOps(&resetsXformStack);
    if (UsdGeomXformCommonAPI(xformable.GetPrim())) {
        for (const UsdGeomXformOp &op : ops) {
            // The common API ops have no suffix and are not inverted
            if (isEditedOpType(op.GetOpType()) && op.GetOpName() == UsdGeomXformOp::GetOpName(op.GetOpType())) {
                return op.GetAttr();
            }
        }
        return xformable.GetXformOpOrderAttr();
    }
    if (ops.size() == 1 && ops[0].GetOpType() == UsdGeomXformOp::TypeTransform) {
        return ops[0].GetAttr();
    }
    return {};
}

/// Why the gizmo can't write the transform of this prim, empty when GetEditedAttribute works
static std::string GetUnsupportedXformReason(const UsdGeomXformable &xformable, XformOpTypePredicate isEditedOpType) {
    if (GetEditedAttribute(xformable, isEditedOpType))
        return {};
    VtTokenArray opOrder;
    xformable.GetXformOpOrderAttr().Get(&opOrder);
    std::string order;
    for (const TfToken &opName : opOrder) {
        const std::string &name = opName.GetString();
        order += (order.empty() ? "" : ", ") + (TfStringStartsWith(name, "xformOp:") ? name.substr(8) : name);
    }
    return "The xform ops don't follow UsdGeomXformCommonAPI, the gizmos can't edit them.\nxformOpOrder: " + order;
}

static std::string FirstLine(const std::string &text) { return text.substr(0, text.find('\n')); }

/// "2 of 5 selected prims" or "The selected prim"
static std::string CountPrefix(size_t count, size_t total) {
    if (total == 1)
        return "The selected prim";
    return std::to_string(count) + " of " + std::to_string(total) + " selected prims";
}

ManipulatorEditability ComputeManipulatorEditability(const std::vector<UsdGeomXformable> &xformables, UsdTimeCode time,
                                                     XformOpTypePredicate isEditedOpType) {
    ManipulatorEditability editability;
    size_t total = 0;
    size_t blockedCount = 0;
    size_t shadowedCount = 0;
    std::string firstBlockedReason;
    std::optional<ShadowingOpinion> firstShadowing;
    UsdStageWeakPtr stage;
    for (const UsdGeomXformable &xformable : xformables) {
        const UsdPrim prim = xformable.GetPrim();
        if (!prim)
            continue;
        ++total;
        std::string blockedReason = GetEditBlockedReason(prim);
        if (blockedReason.empty())
            blockedReason = GetUnsupportedXformReason(xformable, isEditedOpType);
        if (!blockedReason.empty()) {
            if (blockedCount++ == 0)
                firstBlockedReason = blockedReason;
            continue;
        }
        const UsdAttribute attribute = GetEditedAttribute(xformable, isEditedOpType);
        const auto opinion = FindShadowingOpinion(attribute, time, GetEditTargetStrength(prim));
        if (opinion && shadowedCount++ == 0) {
            firstShadowing = opinion;
            stage = prim.GetStage();
        }
    }

    if (total == 0 || (blockedCount == 0 && shadowedCount == 0))
        return editability;

    editability.state = blockedCount == total ? ManipulatorEditability::Blocked : ManipulatorEditability::Warning;
    if (blockedCount == total && total == 1) {
        editability.hudLines.push_back(FirstLine(firstBlockedReason));
        editability.hudLines.push_back(firstBlockedReason.substr(firstBlockedReason.find('\n') + 1));
    } else if (blockedCount > 0) {
        editability.hudLines.push_back(CountPrefix(blockedCount, total) + (blockedCount == total ? " can't be edited: " : " can't be edited and won't move: ") +
                                       FirstLine(firstBlockedReason));
    }
    if (shadowedCount > 0) {
        if (shadowedCount == 1 && total == 1) {
            const std::string message = FormatShadowingOpinion(*firstShadowing, stage);
            editability.hudLines.push_back(FirstLine(message));
            editability.hudLines.push_back(message.substr(message.find('\n') + 1));
        } else {
            editability.hudLines.push_back(CountPrefix(shadowedCount, total) +
                                           ": edits hidden by stronger opinions, the first one in " + firstShadowing->source +
                                           " " + firstShadowing->path.GetString());
        }
        editability.hudLines.push_back("Set the edit target to that layer, or use Edit strongest opinion in the property editor.");
    }
    return editability;
}

ManipulatorEditability ComputeStageCameraEditability(const UsdPrim &camera, UsdTimeCode time) {
    ManipulatorEditability editability;
    if (!camera)
        return editability;
    std::string reason = GetEditBlockedReason(camera);
    if (reason.empty()) {
        // SetFromCamera replaces the xform ops with one matrix op, a stronger opinion on either refuses or hides it
        const UsdGeomXformable xformable(camera);
        const OpinionStrength strength = GetEditTargetStrength(camera);
        auto opinion = FindShadowingOpinion(xformable.GetXformOpOrderAttr(), time, strength);
        if (!opinion)
            opinion = FindShadowingOpinion(camera.GetAttribute(TfToken("xformOp:transform")), time, strength);
        if (!opinion)
            return editability;
        reason = FormatShadowingOpinion(*opinion, camera.GetStage());
    }
    editability.state = ManipulatorEditability::Blocked;
    editability.hudLines.push_back("The camera can't move: " + FirstLine(reason));
    editability.hudLines.push_back(reason.substr(reason.find('\n') + 1));
    return editability;
}

ImColor ManipulatorAxisColor(int axis, int selectedAxis, ManipulatorEditability::State state) {
    if (state == ManipulatorEditability::Blocked)
        return ImColor(ImVec4(ColorAttributeUnauthored));
    if (selectedAxis == axis)
        return ImColor(ImVec4(1.0, 1.0, 0.0, 1.0));
    if (state == ManipulatorEditability::Warning)
        return ImColor(ImVec4(ColorAttributeShadowed));
    return ImColor(ImVec4(axis == Manipulator::XAxis, axis == Manipulator::YAxis, axis == Manipulator::ZAxis, 1.0));
}

void DrawManipulatorEditabilityHud(const ManipulatorEditability &editability) {
    if (editability.hudLines.empty())
        return;
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    const ImVec2 windowPos = ImGui::GetWindowPos();
    const ImVec2 windowSize = ImGui::GetWindowSize();
    const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
    const float margin = ImGui::GetFontSize();
    const float padding = ImGui::GetFontSize() * 0.5f;

    float width = 0.f;
    for (const std::string &line : editability.hudLines) {
        width = std::max(width, ImGui::CalcTextSize(line.c_str()).x);
    }
    const float height = lineHeight * editability.hudLines.size();
    const ImVec2 textPos(windowPos.x + margin + padding, windowPos.y + windowSize.y - margin - padding - height);
    drawList->AddRectFilled(ImVec2(textPos.x - padding, textPos.y - padding),
                            ImVec2(textPos.x + width + padding, textPos.y + height + padding),
                            ImColor(ImVec4(ColorBackgroundDim)), padding);

    const ImColor textColor = editability.state == ManipulatorEditability::Blocked ? ImColor(ImVec4(ColorPrimaryLight))
                                                                                  : ImColor(ImVec4(ColorAttributeShadowed));
    for (size_t i = 0; i < editability.hudLines.size(); ++i) {
        drawList->AddText(ImVec2(textPos.x, textPos.y + lineHeight * i), textColor, editability.hudLines[i].c_str());
    }
}
