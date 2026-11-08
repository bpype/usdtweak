#pragma once
#include <string>
#include <vector>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdGeom/xformOp.h>

struct ImColor;

PXR_NAMESPACE_USING_DIRECTIVE

///
/// Whether the edits of a transform gizmo on the selected prims will work and show in the viewport.
/// Recomputed every frame from the current edit target and time, nothing is cached.
///
struct ManipulatorEditability {
    enum State {
        Editable, ///< all the selected prims can be edited and the edits will show
        Warning,  ///< some edits are refused or hidden by a stronger opinion, the others still apply
        Blocked,  ///< none of the selected prims can be edited, the gizmo can't be dragged
    };
    State state = Editable;
    std::vector<std::string> hudLines; ///< the explanation drawn in the viewport, empty when editable
};

/// True for the xform op types the gizmo writes through UsdGeomXformCommonAPI (translate, rotate or scale)
using XformOpTypePredicate = bool (*)(UsdGeomXformOp::Type);

ManipulatorEditability ComputeManipulatorEditability(const std::vector<UsdGeomXformable> &xformables, UsdTimeCode time,
                                                     XformOpTypePredicate isEditedOpType);

/// Whether viewport navigation can write the transform of a stage camera (UsdGeomCamera::SetFromCamera)
ManipulatorEditability ComputeStageCameraEditability(const UsdPrim &camera, UsdTimeCode time);

/// Axis colour: grey when blocked, yellow when the axis is hovered, orange when edits are hidden, the axis colour otherwise
ImColor ManipulatorAxisColor(int axis, int selectedAxis, ManipulatorEditability::State state);

/// The explanation at the bottom left of the viewport
void DrawManipulatorEditabilityHud(const ManipulatorEditability &editability);
