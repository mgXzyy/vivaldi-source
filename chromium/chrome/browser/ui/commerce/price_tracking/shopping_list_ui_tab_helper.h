// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_COMMERCE_PRICE_TRACKING_SHOPPING_LIST_UI_TAB_HELPER_H_
#define CHROME_BROWSER_UI_COMMERCE_PRICE_TRACKING_SHOPPING_LIST_UI_TAB_HELPER_H_

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "components/commerce/core/shopping_service.h"
#include "components/commerce/core/subscriptions/subscriptions_observer.h"
#include "components/image_fetcher/core/request_metadata.h"
#include "components/prefs/pref_registry_simple.h"
#include "content/public/browser/web_contents_observer.h"
#include "content/public/browser/web_contents_user_data.h"
#include "ui/gfx/image/image.h"

class GURL;
class SidePanelUI;
namespace bookmarks {
class BookmarkModel;
}

namespace content {
class NavigationHandle;
class WebContents;
}  // namespace content

namespace image_fetcher {
class ImageFetcher;
}

namespace views {
class View;
}  // namespace views

namespace commerce {

struct CommerceSubscription;

// This tab helper is used to update and maintain the state of the shopping list
// and price tracking UI on desktop.
// TODO(b:283833590): Rename this class since it serves for all shopping
// features now.
class ShoppingListUiTabHelper
    : public content::WebContentsObserver,
      public content::WebContentsUserData<ShoppingListUiTabHelper>,
      public SubscriptionsObserver {
 public:
  ~ShoppingListUiTabHelper() override;
  ShoppingListUiTabHelper(const ShoppingListUiTabHelper& other) = delete;
  ShoppingListUiTabHelper& operator=(const ShoppingListUiTabHelper& other) =
      delete;

  static void RegisterProfilePrefs(PrefRegistrySimple* registry);

  // Get the image for the last fetched product URL. A reference to this object
  // should not be kept directly, if one is needed, a copy should be made.
  virtual const gfx::Image& GetProductImage();
  // Return whether the PriceTrackingIconView is visible.
  virtual bool ShouldShowPriceTrackingIconView();
  // Return whether the PriceInsightsIconView is visible.
  virtual bool ShouldShowPriceInsightsIconView();

  // The URL for the last fetched product image. A reference to this object
  // should not be kept directly, if one is needed, a copy should be made.
  const GURL& GetProductImageURL();

  // Returns whether the current page has a product that is being price tracked.
  virtual bool IsPriceTracking();

  // content::WebContentsObserver implementation
  void DidFinishNavigation(
      content::NavigationHandle* navigation_handle) override;
  void DidStopLoading() override;

  // SubscriptionsObserver
  void OnSubscribe(const CommerceSubscription& subscription,
                   bool succeeded) override;

  void OnUnsubscribe(const CommerceSubscription& subscription,
                     bool succeeded) override;

  // Update this tab helper (and associated observers) to use a different
  // shopping service for the sake of testing.
  void SetShoppingServiceForTesting(ShoppingService* shopping_service);

  // Set the price tracking state for the product on the current page.
  virtual void SetPriceTrackingState(bool enable,
                                     bool is_new_bookmark,
                                     base::OnceCallback<void(bool)> callback);
  void OnPriceInsightsIconClicked();

  // Return the PriceInsightsInfo for the last fetched product URL. A reference
  // to this object should not be kept directly, if one is needed, a copy should
  // be made.
  virtual const absl::optional<PriceInsightsInfo>& GetPriceInsightsInfo();

 protected:
  ShoppingListUiTabHelper(content::WebContents* contents,
                          ShoppingService* shopping_service,
                          bookmarks::BookmarkModel* model,
                          image_fetcher::ImageFetcher* image_fetcher);

  const absl::optional<bool>& GetPendingTrackingStateForTesting();

  virtual std::unique_ptr<views::View> CreateShoppingInsightsWebView();

 private:
  friend class content::WebContentsUserData<ShoppingListUiTabHelper>;
  friend class ShoppingListUiTabHelperTest;

  void HandleProductInfoResponse(const GURL& url,
                                 const absl::optional<const ProductInfo>& info);

  void HandlePriceInsightsInfoResponse(
      const GURL& url,
      const absl::optional<PriceInsightsInfo>& info);

  void HandleImageFetcherResponse(
      const GURL image_url,
      const gfx::Image& image,
      const image_fetcher::RequestMetadata& request_metadata);

  void UpdatePriceTrackingIconView();

  void UpdatePriceInsightsIconView();

  // Update the flag tracking the price tracking state of the product from
  // subscriptions.
  void UpdatePriceTrackingStateFromSubscriptions();

  void HandleSubscriptionChange(const CommerceSubscription& sub);

  void TriggerUpdateForIconView();

  bool ShouldIgnoreSameUrlNavigation();

  bool IsSameDocumentWithSameCommittedUrl(
      content::NavigationHandle* navigation_handle);

  // Make the ShoppingInsights entry available in the side panel.
  void MakeShoppingInsightsSidePanelAvailable();

  // Make the ShoppingInsights entry unavailable in the side panel. If the
  // ShoppingInsights side panel is currently showing, close the side panel
  // first.
  void MakeShoppingInsightsSidePanelUnavailable();

  SidePanelUI* GetSidePanelUI() const;

  void DelayUpdateForIconView();

  // The shopping service is tied to the lifetime of the browser context
  // which will always outlive this tab helper.
  raw_ptr<ShoppingService, DanglingUntriaged> shopping_service_;
  raw_ptr<bookmarks::BookmarkModel> bookmark_model_;
  raw_ptr<image_fetcher::ImageFetcher> image_fetcher_;

  // The URL of the last product image that was fetched.
  GURL last_fetched_image_url_;

  // The last image that was fetched. See |last_image_fetched_url_| for the
  // URL that was used.
  gfx::Image last_fetched_image_;

  // Whether the product shown on the current page is tracked by the user.
  bool is_cluster_id_tracked_by_user_{false};

  // The cluster ID for the current page, if applicable.
  absl::optional<uint64_t> cluster_id_for_page_;

  // A flag indicating whether the initial navigation has committed for the web
  // contents. This is used to ensure product info is fetched when a tab is
  // being restored.
  bool is_initial_navigation_committed_{false};

  // This represents the desired state of the tracking icon prior to getting the
  // callback from the (un)subscribe event. If no value, there is no pending
  // state, otherwise true means "tracking" and false means "not tracking".
  absl::optional<bool> pending_tracking_state_;

  // A flag to indicating whether the first load after a navigation has
  // completed.
  bool is_first_load_for_nav_finished_{false};

  // The url from the previous successful main frame navigation. This will be
  // empty if this is the first navigation for this tab or post-restart.
  GURL previous_main_frame_url_;

  // The PriceInsightsInfo associated with the last committed URL.
  absl::optional<PriceInsightsInfo> price_insights_info_;

  // Automatically remove this observer from its host when destroyed.
  base::ScopedObservation<ShoppingService, SubscriptionsObserver>
      scoped_observation_{this};

  base::WeakPtrFactory<ShoppingListUiTabHelper> weak_ptr_factory_{this};

  WEB_CONTENTS_USER_DATA_KEY_DECL();
};

}  // namespace commerce

#endif  // CHROME_BROWSER_UI_COMMERCE_PRICE_TRACKING_SHOPPING_LIST_UI_TAB_HELPER_H_
