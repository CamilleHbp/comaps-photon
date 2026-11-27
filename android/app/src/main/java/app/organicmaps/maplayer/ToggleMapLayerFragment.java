package app.organicmaps.maplayer;

import android.content.Context;
import app.organicmaps.bookmarks.BookmarkCategoriesActivity;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import app.organicmaps.MwmApplication;
import app.organicmaps.R;
import app.organicmaps.sdk.maplayer.Mode;
import app.organicmaps.sdk.util.SharedPropertiesUtils;
import app.organicmaps.util.ThemeSwitcher;
import app.organicmaps.util.Utils;
import app.organicmaps.util.bottomsheet.MenuBottomSheetFragment;
import java.util.ArrayList;
import java.util.List;

public class ToggleMapLayerFragment extends Fragment
{
  private static final String LAYERS_MENU_ID = "LAYERS_MENU_BOTTOM_SHEET";
  private MapButtonsController mMapButtonsController;

  @Nullable
  @Override
  public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container,
                           @Nullable Bundle savedInstanceState)
  {
    mMapButtonsController =
        (MapButtonsController) requireActivity().getSupportFragmentManager().findFragmentById(R.id.map_buttons);
    return new SavedPlacesLayersView(requireContext(), getLayersItems(),
        () -> { BookmarkCategoriesActivity.start(requireActivity()); closeLayerBottomSheet(); },
        this::closeLayerBottomSheet,
        () -> { app.organicmaps.bookmarks.OsmAndImportActivity.start(requireActivity()); closeLayerBottomSheet(); });
  }

  private List<LayerBottomSheetItem> getLayersItems()
  {
    List<Mode> availableLayers = LayersUtils.getAvailableLayers();
    List<LayerBottomSheetItem> items = new ArrayList<>();
    for (Mode layer : availableLayers)
    {
      items.add(LayerBottomSheetItem.create(requireContext(), layer, this::onItemClick));
    }
    return items;
  }

  private void onItemClick(@NonNull View v, @NonNull LayerBottomSheetItem item)
  {
    Mode mode = item.getMode();
    Context context = v.getContext();
    SharedPropertiesUtils.setLayerMarkerShownForLayerMode(mode);
    mode.setEnabled(context, !mode.isEnabled(context));
    // TODO: dirty hack :(
    if (mode == Mode.OUTDOORS)
      ThemeSwitcher.INSTANCE.restart(true);
    mMapButtonsController.updateLayerButton();
    if (MwmApplication.from(context).getIsolinesManager().shouldShowNotification())
      Utils.showSnackbar(context, v.getRootView(), R.string.isolines_toast_zooms_1_10);
  }

  private void closeLayerBottomSheet()
  {
    MenuBottomSheetFragment bottomSheet =
        (MenuBottomSheetFragment) requireActivity().getSupportFragmentManager().findFragmentByTag(LAYERS_MENU_ID);
    if (bottomSheet != null)
      bottomSheet.dismiss();
  }
}
