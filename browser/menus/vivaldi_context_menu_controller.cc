//
// Copyright (c) 2019 Vivaldi Technologies AS. All rights reserved.
//
#include "browser/menus/vivaldi_context_menu_controller.h"
#include "base/base64.h"
#include "base/strings/utf_string_conversions.h"
#include "browser/menus/vivaldi_menu_enums.h"
#include "browser/menus/vivaldi_render_view_context_menu.h"
#include "browser/vivaldi_browser_finder.h"
#include "chrome/browser/favicon/favicon_service_factory.h"
#include "chrome/common/chrome_switches.h"
#include "chrome/grit/generated_resources.h"
#include "components/favicon/core/favicon_service.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/context_menu_params.h"
#include "content/public/browser/web_contents.h"
#include "extensions/api/menubar_menu/menubar_menu_api.h"
#include "extensions/tools/vivaldi_tools.h"
#include "include/core/SkBitmap.h"
#include "skia/ext/image_operations.h"
#include "ui/gfx/favicon_size.h"
#include "ui/vivaldi_context_menu.h"
#include "vivaldi/prefs/vivaldi_gen_prefs.h"

namespace vivaldi {

#define ICON_SIZE 16

ContextMenuController::ContextMenuController(
    content::WebContents* window_web_contents,
    VivaldiRenderViewContextMenu* rv_context_menu,
    absl::optional<Params> params)
    : window_web_contents_(window_web_contents),
      rv_context_menu_(rv_context_menu),
      with_developer_tools_(!rv_context_menu_ ||
                            rv_context_menu_->SupportsInspectTools()),
      params_(std::move(params)) {
  using Origin = extensions::vivaldi::context_menu::Origin;

  // We set the height to zero depending on where we want chrome code to
  // position the menu. Chrome does not support placing a menu to the right or
  // left of the "forbidden zone" (the rect we set up here), so for those
  // configuations eg, a stack menu from a tab stack in a vertical bar, we have
  // SetPosition() below which is called from chrome after the menu size is
  // known.
  gfx::Point point(params_->properties.rect.x, params_->properties.rect.y);
  int height = params_->properties.rect.height;
  if (params_->properties.origin == Origin::ORIGIN_TOPLEFT ||
      params_->properties.origin == Origin::ORIGIN_TOPRIGHT) {
    height = 0;
  }
  int width = params_->properties.rect.width;

  gfx::RectF rect(point.x(), point.y(), width, height);
  // Document context menu coordinates are not affected by UI zoom.
  if (!rv_context_menu_) {
    FromUICoordinates(window_web_contents_, &rect);
  }
  rect_ = gfx::Rect(round(rect.x()), round(rect.y()), round(rect.width()),
                    round(rect.height()));

  developertools_controller_.reset(
      new DeveloperToolsMenuController(window_web_contents_, rect_.origin()));

  if (rv_context_menu_) {
    rv_context_menu_->SetModelDelegate(this);
    rv_context_menu_->SetMenuDelegate(this);
  }

  show_shortcuts_ = GetProfile()->GetPrefs()->GetBoolean(
      vivaldiprefs::kKeyboardShortcutsEnable);
}

ContextMenuController::~ContextMenuController() {}

Profile* ContextMenuController::GetProfile() {
  return Profile::FromBrowserContext(window_web_contents_->GetBrowserContext());
}

bool ContextMenuController::Show() {
  using Origin = extensions::vivaldi::context_menu::Origin;

  // Mac needs the views version for certain origins as we can not position the
  // menu properly on mac/cocoa.
  bool force_views = params_->properties.origin != Origin::ORIGIN_POINTER;

  menu_.reset(CreateVivaldiContextMenu(window_web_contents_, nullptr, rect_,
                                       force_views, rv_context_menu_));

  if (rv_context_menu_) {
    menu_->SetParentView(rv_context_menu_->parent_view());
  }

  extensions::MenubarMenuAPI::SendOpen(GetProfile(), 0);

  // Populate model.
  InitModel();

  // We do not know if count is 0 until after InitModel().
  if (root_menu_model_->GetItemCount() == 0) {
    MenuClosed(root_menu_model_);
    // We have done a delete this
    return false;
  }

  menu_->Init(root_menu_model_, force_views ? this : nullptr);

  has_shown_ = menu_->Show();
  if (!has_shown_) {
    MenuClosed(root_menu_model_);
    // We have done a delete this
    return false;
  }

  return true;
}

void ContextMenuController::InitModel() {
  namespace context_menu = extensions::vivaldi::context_menu;

  if (rv_context_menu_) {
    root_menu_model_ = rv_context_menu_->root_menu_model();
  } else {
    root_menu_model_ = new ui::SimpleMenuModel(this);
    models_.push_back(base::WrapUnique(root_menu_model_.get()));
  }

  bool dark_text_color = menu_->HasDarkTextColor();

  // Add items from JS
  for (const context_menu::Element& child : params_->properties.children) {
    PopulateModel(child, dark_text_color, root_menu_model_);
  }

  // Add developer tools items
  if (with_developer_tools_) {
    developertools_controller_->PopulateModel(root_menu_model_);
  }

  SanitizeModel(root_menu_model_);
}

std::string ContextMenuController::GetEmptyIcon() {
  // Base64 of a 1x1 transparent PNG.
  std::string icon =
  "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAACXBIWXMAAAsTAAALEwEAmpwYAAA"\
  "AAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAAAQSURBVHgBAQUA+v8AAAAAAAAFAAFkeJU4AA"\
  "AAAElFTkSuQmCC";
  return icon;
}


void ContextMenuController::PopulateModel(const Element& child,
                                          bool dark_text_color,
                                          ui::SimpleMenuModel* menu_model) {
  namespace context_menu = extensions::vivaldi::context_menu;
  if (child.item) {
    const Item& item = *child.item;
    int id = item.id + IDC_VIV_MENU_FIRST + 1;
    if (rv_context_menu_) {
      if (item.action) {
        // Check if action is mapped with a known command id. If this is the
        // case the action is in most cases executed in C++ only.
        int idc = rv_context_menu_->GetStaticIdForAction(*item.action);
        if (idc != -1) {
          // JS can (depending on idc) be called using second id.
          rv_context_menu_->SetCommandId(idc, id);
          id = idc;
        } else {
          // No second id. JS will always be called with the first.
          rv_context_menu_->SetCommandId(id, -1);
        }
      } else {
        // No second id. JS will always be called with the first.
        rv_context_menu_->SetCommandId(id, -1);
      }
    }
    const std::u16string label = base::UTF8ToUTF16(item.name);
    switch (item.type) {
      case context_menu::ITEM_TYPE_COMMAND:
        menu_model->AddItem(id, label);
        break;
      case context_menu::ITEM_TYPE_CHECKBOX:
        menu_model->AddCheckItem(id, label);
        id_to_checked_map_[id] = item.checked.value_or(false);
        break;
      case context_menu::ITEM_TYPE_RADIO:
        menu_model->AddRadioItem(id, label, item.radiogroup.value_or(0));
        id_to_checked_map_[id] = item.checked.value_or(false);
        break;
      case context_menu::ITEM_TYPE_FOLDER: {
        ui::SimpleMenuModel* child_menu_model;
        if (rv_context_menu_) {
          child_menu_model = new ui::SimpleMenuModel(rv_context_menu_);
        } else {
          child_menu_model = new ui::SimpleMenuModel(this);
        }
        models_.push_back(base::WrapUnique(child_menu_model));

        menu_model->AddSubMenu(id, label, child_menu_model);
        for (const Element& it : *child.children) {
          PopulateModel(it, dark_text_color, child_menu_model);
        }
        SanitizeModel(child_menu_model);
      } break;
      case context_menu::ITEM_TYPE_NONE:
        return;
    }
    if (item.shortcut && !item.shortcut->empty()) {
      id_to_accelerator_map_[id] =
          ::vivaldi::ParseShortcut(*item.shortcut, true);
    }
    // We will show an accelerator unless explicitly registered to false. Note
    // that we transfer display information in each item, but all items are set
    // to the same value within a menu. So we set the flag that controls the
    // whole menu if an item is false.
    if (item.showshortcut && *item.showshortcut == false) {
      show_shortcuts_ = false;
    }
    if (item.action.has_value()) {
      id_to_action_map_[id] = item.action.value();
    }
    if (item.enabled.has_value()) {
      id_to_enabled_map_[id] = item.enabled.value();
    }
    if (item.url && !item.url->empty()) {
      // Set default document icon
      SetIcon(id, params_->properties.icons.at(0), menu_model);
      // Attempt loading a favicon that will replace the default.
      id_to_url_map_[id] = item.url.value_or("");
      LoadFavicon(id, item.url.value(), true);
    } else if (item.icons && item.icons->size() == 2) {
      const std::string icon = item.icons->at(dark_text_color ? 0 : 1);
      if (GURL(icon).SchemeIsHTTPOrHTTPS()) {
        // Loading a favicon from a url (or local cache) is asynchronous. We
        // have to ensure there is at least one icon present in the model before
        // showing the menu to ensure the model can signal to the layout to set
        // aside proper space for it. So we load an empty icon.
        // We use a SimpleMenuModel that we do not create ourself for the
        // document menu so we can not override it (if we did we could
        // have reimplemented SimpleMenuModel::HasIcons()).
        SetIcon(id, GetEmptyIcon(), menu_model);
        LoadFavicon(id, icon, false);
      } else {
        // Allows for hardcoded png data.
        SetIcon(id, icon, menu_model);
      }
    } else if (rv_context_menu_ && item.action) {
      ui::ImageModel img = rv_context_menu_->GetImageForAction(*item.action);
      if (!img.IsEmpty()) {
        menu_model->SetIcon(menu_model->GetIndexOfCommandId(id).value(), img);
      }
    }
  } else if (child.container) {
    const Container& container = *child.container;
    int id = container.id + IDC_VIV_MENU_FIRST + 1;
    switch (container.content) {
      case context_menu::CONTAINER_CONTENT_PWA:
        pwa_controller_ = std::make_unique<PWAMenuController>(
            FindBrowserForEmbedderWebContents(window_web_contents_));
        pwa_controller_->PopulateModel(
            GetContainerModel(container, id, menu_model));
        break;
      default:
        if (rv_context_menu_ &&
            rv_context_menu_->HasContainerContent(container)) {
          rv_context_menu_->PopulateContainer(
              container, id, GetContainerModel(container, id, menu_model));
        }
        break;
    }
  } else if (child.separator) {
    menu_model->AddSeparator(ui::NORMAL_SEPARATOR);
  }
}

ui::SimpleMenuModel* ContextMenuController::GetContainerModel(
    const Container& container,
    int id,
    ui::SimpleMenuModel* menu_model) {
  namespace context_menu = extensions::vivaldi::context_menu;

  if (container.mode == context_menu::CONTAINER_MODE_FOLDER) {
    const std::u16string label = base::UTF8ToUTF16(container.name);
    ui::SimpleMenuModel* child_menu_model;
    if (rv_context_menu_) {
      child_menu_model = new ui::SimpleMenuModel(rv_context_menu_);
      rv_context_menu_->SetCommandId(id, -1);
    } else {
      child_menu_model = new ui::SimpleMenuModel(this);
    }
    models_.push_back(base::WrapUnique(child_menu_model));
    container_folder_models_.push_back(child_menu_model);
    menu_model->AddSubMenu(id, label, child_menu_model);
    return child_menu_model;
  } else {
    return menu_model;
  }
}

void ContextMenuController::SanitizeModel(ui::SimpleMenuModel* menu_model) {
  for (int i = menu_model->GetItemCount() - 1; i >= 0; i--) {
    if (menu_model->GetTypeAt(i) == ui::MenuModel::TYPE_SEPARATOR) {
      menu_model->RemoveItemAt(i);
    } else {
      break;
    }
  }
}

// Called from chrome when menu size is known.
void ContextMenuController::SetPosition(gfx::Rect* menu_bounds,
                                        const gfx::Rect& monitor_bounds,
                                        const gfx::Rect& anchor_bounds) const {
  using Origin = extensions::vivaldi::context_menu::Origin;

  if (params_->properties.origin == Origin::ORIGIN_TOPRIGHT) {
    // Place left edge of menu to the right of anchor area. If not enough room
    // to fit inside monitor area move it to the left of the anchor area.
    menu_bounds->set_x(anchor_bounds.right());
    menu_bounds->set_y(anchor_bounds.bottom());
    if (menu_bounds->right() > monitor_bounds.right()) {
      menu_bounds->set_x(anchor_bounds.x() - menu_bounds->width());
    }
  } else if (params_->properties.origin == Origin::ORIGIN_TOPLEFT) {
    // Place right edge of menu to the left of anchor area. If not enough room
    // to fit inside monitor area move it to the right of the anchor area.
    menu_bounds->set_x(anchor_bounds.x() - menu_bounds->width());
    menu_bounds->set_y(anchor_bounds.bottom());
    if (menu_bounds->x() < monitor_bounds.x()) {
      menu_bounds->set_x(anchor_bounds.right());
    }
  }

  // Fallback code in chrome will ensure the menu is within the monitor area so
  // we do not test more than the last adjustment above.
}

void ContextMenuController::SetIcon(int command_id,
                                    const std::string& icon,
                                    ui::SimpleMenuModel* menu_model) {
  if (icon.length() > 0) {
    std::string png_data;
    if (base::Base64Decode(icon, &png_data)) {
      gfx::Image img = gfx::Image::CreateFrom1xPNGBytes(
          reinterpret_cast<const unsigned char*>(png_data.c_str()),
          png_data.length());
      if (img.Width() > ICON_SIZE || img.Height() > ICON_SIZE) {
        int width = img.Width();
        int height = img.Height();
        gfx::CalculateFaviconTargetSize(&width, &height);
        SkBitmap bitmap(*img.ToSkBitmap());
        img = gfx::Image::CreateFrom1xBitmap(skia::ImageOperations::Resize(
            bitmap, skia::ImageOperations::RESIZE_GOOD, width, height));
      }

      menu_model->SetIcon(menu_model->GetIndexOfCommandId(command_id).value(),
                          ui::ImageModel::FromImage(img));
    }
  }
}

void ContextMenuController::LoadFavicon(int command_id,
                                        const std::string& url,
                                        bool is_page) {
  if (!favicon_service_) {
    favicon_service_ = FaviconServiceFactory::GetForProfile(
        GetProfile(), ServiceAccessType::EXPLICIT_ACCESS);
    if (!favicon_service_)
      return;
  }

  favicon_base::FaviconImageCallback callback =
      base::BindOnce(&ContextMenuController::OnFaviconDataAvailable,
                     base::Unretained(this), command_id);

  if (is_page) {
    favicon_service_->GetFaviconImageForPageURL(GURL(url), std::move(callback),
                                                &cancelable_task_tracker_);
  } else {
    favicon_service_->GetFaviconImage(GURL(url), std::move(callback),
                                      &cancelable_task_tracker_);
  }
}

void ContextMenuController::OnFaviconDataAvailable(
    int command_id,
    const favicon_base::FaviconImageResult& image_result) {
  if (!image_result.image.IsEmpty()) {
    // Update the menu directly so that a visible menu will be updated, The
    // MenuItemView class we use to paint the menu does not support dynamic
    // updates of icons through the model.
    menu_->SetIcon(image_result.image, command_id);
    // We have to update the model as well so that if a menu reloads utself
    // due to a dynamic update the model can provide the state at that point.
    auto index = root_menu_model_->GetIndexOfCommandId(command_id);
    if (index.has_value()) {
      root_menu_model_->SetIcon(index.value(),
        ui::ImageModel::FromImage(image_result.image));
    } else {
      for (unsigned i = 0; i < models_.size(); i++ ) {
        ui::SimpleMenuModel* model = models_[i].get();
        index = model->GetIndexOfCommandId(command_id);
        if (index.has_value()) {
          model->SetIcon(index.value(), ui::ImageModel::FromImage(image_result.image));
          break;
        }
      }
    }
  }
}

bool ContextMenuController::IsCommandIdChecked(int command_id) const {
  auto it = id_to_checked_map_.find(command_id);
  return it != id_to_checked_map_.end() ? it->second : false;
}

bool ContextMenuController::IsCommandIdEnabled(int command_id) const {
  auto it = id_to_enabled_map_.find(command_id);
  return it != id_to_enabled_map_.end() ? it->second : true;
}

bool ContextMenuController::IsItemForCommandIdDynamic(int command_id) const {
  if (pwa_controller_ &&
      pwa_controller_->IsItemForCommandIdDynamic(command_id)) {
    return true;
  }
  return false;
}

// This function only needs to return a valid string for dynamic items.
std::u16string ContextMenuController::GetLabelForCommandId(
    int command_id) const {
  if (pwa_controller_ &&
      pwa_controller_->IsItemForCommandIdDynamic(command_id)) {
    return pwa_controller_->GetLabelForCommandId(command_id);
  }
  return std::u16string();
}

bool ContextMenuController::GetAcceleratorForCommandId(
    int command_id,
    ui::Accelerator* accelerator) const {
  auto it = id_to_accelerator_map_.find(command_id);
  if (it != id_to_accelerator_map_.end()) {
    *accelerator = it->second;
    return true;
  }

  if (with_developer_tools_ &&
      developertools_controller_->GetAcceleratorForCommandId(command_id,
                                                             accelerator)) {
    return true;
  }
  return false;
}

void ContextMenuController::VivaldiCommandIdHighlighted(int command_id) {
  auto it = id_to_url_map_.find(command_id);
  extensions::MenubarMenuAPI::SendHover(GetProfile(),
      params_->properties.window_id,
      it != id_to_url_map_.end() ? it->second : "");
}

void ContextMenuController::ExecuteCommand(int command_id, int event_flags) {
  if (developertools_controller_->HandleCommand(command_id)) {
  } else if (pwa_controller_ && pwa_controller_->HandleCommand(command_id)) {
  } else {
    extensions::MenubarMenuAPI::SendAction(GetProfile(), command_id,
                                           event_flags);
  }
}

void ContextMenuController::OnMenuWillShow(ui::SimpleMenuModel* source) {
  if (rv_context_menu_) {
    for (unsigned i = 0; i < container_folder_models_.size(); i++) {
      if (container_folder_models_.at(i) == source &&
          source->GetItemCount() == 0) {
        rv_context_menu_->ContainerWillOpen(source);
        break;
      }
    }
  }
}

bool ContextMenuController::GetShowShortcuts() {
  return show_shortcuts_;
}

// Called when the VivaldiRenderViewContextMenu object is destroyed by events
// outside the menu itself (eg, when parent view is destructed).
// A simple way to test this is to use a timeout when closing a tab and open a
// document context menu before it expires.
void ContextMenuController::OnDestroyed(VivaldiRenderViewContextMenu* menu) {
  rv_context_menu_->SetModelDelegate(nullptr);
  rv_context_menu_->SetMenuDelegate(nullptr);
  Delete();
}

void ContextMenuController::MenuClosed(ui::SimpleMenuModel* source) {
  if (source == root_menu_model_) {
    if (rv_context_menu_) {
      // The object should no longer access us.
      rv_context_menu_->SetModelDelegate(nullptr);
      rv_context_menu_->SetMenuDelegate(nullptr);
      // And do not access this object anymore as its root model has signalled.
      rv_context_menu_ = nullptr;
    } else {
      // Prevent the root model we have created from accessing us while we wait
      // to close.
      source->SetMenuModelDelegate(nullptr);
    }

    if (has_shown_) {
      // TODO(espen): Closing by clicking outside the menu triggers a crash on
      // Mac. It seems to be access to data after a "delete this" which the
      // OnClosed call to the delegate starts, but the crash log is hard to make
      // sense of.
      timer_.reset(new base::OneShotTimer());
      timer_->Start(FROM_HERE, base::Milliseconds(1),
                    base::BindOnce(&ContextMenuController::Delete,
                                   base::Unretained(this)));
    } else {
      Delete();
    }
  }
}

void ContextMenuController::Delete() {
  root_menu_model_ = nullptr;
  extensions::MenubarMenuAPI::SendClose(GetProfile());
  delete this;
}

}  // namespace vivaldi
