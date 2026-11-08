#pragma once
#include <cassert>
#include <pxr/usd/sdf/listEditorProxy.h>
#include <pxr/usd/sdf/reference.h>
#include <pxr/usd/sdf/listOp.h>
#include <optional>
#include <string>
#include <pxr/usd/usd/attribute.h>
#include <pxr/usd/usd/prim.h>

PXR_NAMESPACE_USING_DIRECTIVE

// ExtraArgsT is used to pass additional arguments as the function passed as visitor
// might need more than the operation and the item
template <typename PolicyT, typename FuncT, typename... ExtraArgsT>
static void IterateListEditorItems(const SdfListEditorProxy<PolicyT> &listEditor, const FuncT &call, ExtraArgsT... args) {
    // TODO: should we check if the list is already all explicit ??
    for (const typename PolicyT::value_type &item : listEditor.GetExplicitItems()) {
        call(SdfListOpTypeExplicit, item, args...);
    }
    for (const typename PolicyT::value_type &item : listEditor.GetOrderedItems()) {
        call(SdfListOpTypeOrdered, item, args...);
    }
    for (const typename PolicyT::value_type &item : listEditor.GetAddedItems()) {
        call(SdfListOpTypeAdded, item, args...); // return "add" as TfToken instead ?
    }
    for (const typename PolicyT::value_type &item : listEditor.GetPrependedItems()) {
        call(SdfListOpTypePrepended, item, args...);
    }
    for (const typename PolicyT::value_type &item : listEditor.GetAppendedItems()) {
        call(SdfListOpTypeAppended, item, args...);
    }
    for (const typename PolicyT::value_type &item : listEditor.GetDeletedItems()) {
        call(SdfListOpTypeDeleted, item, args...);
    }
}

/// The list available on a SdfListEditor
constexpr int GetListEditorOperationSize() { return 6; }

template <typename IntOrSdfListOpT> inline const char *GetListEditorOperationName(IntOrSdfListOpT index) {
    constexpr const char *names[GetListEditorOperationSize()] = {"explicit", "add", "delete", "ordered", "prepend", "append"};
    return names[static_cast<int>(index)];
}

template <typename IntOrSdfListOpT> inline const char *GetListEditorOperationAbbreviation(IntOrSdfListOpT index) {
    constexpr const char *names[GetListEditorOperationSize()] = {"Ex", "Ad", "De", "Or", "Pr", "Ap"};
    return names[static_cast<int>(index)];
}

template <typename PolicyT>
inline void CreateListEditorOperation(SdfListEditorProxy<PolicyT> &&listEditor, SdfListOpType op,
                                      typename SdfListEditorProxy<PolicyT>::value_type item) {
    switch (op) {
    case SdfListOpTypeAdded:
        listEditor.GetAddedItems().push_back(item);
        break;
    case SdfListOpTypePrepended:
        listEditor.GetPrependedItems().push_back(item);
        break;
    case SdfListOpTypeAppended:
        listEditor.GetAppendedItems().push_back(item);
        break;
    case SdfListOpTypeDeleted:
        listEditor.GetDeletedItems().push_back(item);
        break;
    case SdfListOpTypeExplicit:
        listEditor.GetExplicitItems().push_back(item);
        break;
    case SdfListOpTypeOrdered:
        listEditor.GetOrderedItems().push_back(item);
        break;
    default:
        assert(0);
    }
}

template <typename ListEditorT, typename OpOrIntT> inline auto GetSdfListOpItems(ListEditorT &listEditor, OpOrIntT op_) {
    const SdfListOpType op = static_cast<SdfListOpType>(op_);
    if (op == SdfListOpTypeOrdered) {
        return listEditor.GetOrderedItems();
    } else if (op == SdfListOpTypeAppended) {
        return listEditor.GetAppendedItems();
    } else if (op == SdfListOpTypeAdded) {
        return listEditor.GetAddedItems();
    } else if (op == SdfListOpTypePrepended) {
        return listEditor.GetPrependedItems();
    } else if (op == SdfListOpTypeDeleted) {
        return listEditor.GetDeletedItems();
    }
    return listEditor.GetExplicitItems();
};


template <typename ListEditorT, typename OpOrIntT, typename ItemsT> inline void SetSdfListOpItems(ListEditorT &listEditor, OpOrIntT op_, const ItemsT &items) {
    const SdfListOpType op = static_cast<SdfListOpType>(op_);
    if (op == SdfListOpTypeOrdered) {
        listEditor.SetOrderedItems(items);
    } else if (op == SdfListOpTypeAppended) {
        listEditor.SetAppendedItems(items);
    } else if (op == SdfListOpTypeAdded) {
        listEditor.SetAddedItems(items);
    } else if (op == SdfListOpTypePrepended) {
        listEditor.SetPrependedItems(items);
    } else if (op == SdfListOpTypeDeleted) {
        listEditor.SetDeletedItems(items);
    } else {
        listEditor.SetExplicitItems(items);
    }
};

// Look for a new token. If prefix ends with a number, it will increase its value until
// a valid token is found
std::string FindNextAvailableTokenString(std::string prefix);

// Find usd file format extensions and returns them prefixed with a dot
const std::vector<std::string> GetUsdValidExtensions();


///
/// Checks answering "will an edit of this prim in the current edit target work, and will it show".
/// Shared by the stage property editor and the viewport manipulators so they agree and explain the same way.
/// Everything is recomputed on demand, nothing is cached.
///

/// The reasons USD refuses to author on a prim in the current edit target
enum class EditBlocked { None, InstanceProxy, Prototype, NoEditTarget, PrimNotMapped, LayerReadOnly, FormatNotEditable };

EditBlocked GetEditBlocked(const UsdPrim &prim);

/// The widgets authoring on the prim are greyed when this is false
inline bool IsPrimEditable(const UsdPrim &prim) { return GetEditBlocked(prim) == EditBlocked::None; }

/// Why the prim can't be edited (first line) and how to make it editable (second line), empty when it is editable
std::string GetEditBlockedReason(const UsdPrim &prim);

/// Position of an opinion in the strength order of a prim index: the node, then the layer in the node layer stack.
/// Lower is stronger.
struct OpinionStrength {
    int node = -1;
    int layer = -1; // -1 with a valid node when the layer is unknown, for value clips
    bool HasNode() const { return node >= 0; }
};

/// Where the current edit target writes the opinions of the prim. No node when the edit target site isn't in the
/// prim index, we can't tell how strong the edit will be.
OpinionStrength GetEditTargetStrength(const UsdPrim &prim);

/// The opinion providing the attribute value when it is stronger than the edit target
struct ShadowingOpinion {
    std::string source; ///< layer display name, or the value clips description
    SdfPath path;       ///< attribute path in the source
};

/// The opinion hiding the edits of the attribute value, none when the edits will show or when we can't tell
std::optional<ShadowingOpinion> FindShadowingOpinion(const UsdAttribute &attribute, UsdTimeCode time,
                                                     const OpinionStrength &editTargetStrength);

/// "This value comes from a stronger opinion: <source> <path>\nEdits in the edit target <layer> are hidden by it."
std::string FormatShadowingOpinion(const ShadowingOpinion &opinion, const UsdStageWeakPtr &stage);
