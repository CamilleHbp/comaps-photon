package app.organicmaps.maplayer;

import android.content.Context;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.View;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.PopupMenu;
import android.widget.ScrollView;
import android.widget.TextView;
import app.organicmaps.R;
import app.organicmaps.sdk.bookmarks.data.BookmarkCategory;
import app.organicmaps.sdk.bookmarks.data.BookmarkManager;
import app.organicmaps.sdk.bookmarks.data.DataChangedListener;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.switchmaterial.SwitchMaterial;
import java.util.Locale;

/** Map controls backed by the same persistent visibility as the Favorites screen. */
final class SavedPlacesLayersView extends LinearLayout implements DataChangedListener
{
  private final androidx.recyclerview.widget.RecyclerView rows;
  private final java.util.List<BookmarkCategory> visibleRows = new java.util.ArrayList<>();
  private final PlacesAdapter adapter = new PlacesAdapter();
  private final EditText search;
  private final Runnable dismiss;

  SavedPlacesLayersView(Context context, Runnable manage, Runnable dismiss, Runnable importPlaces)
  {
    super(context);
    this.dismiss = dismiss;
    setOrientation(VERTICAL);
    int padding = Math.round(16 * getResources().getDisplayMetrics().density);
    setPadding(padding, 0, padding, padding);
    TextView title = new TextView(context);
    title.setText(R.string.saved_layers_title);
    title.setTextSize(20);
    addView(title);
    LinearLayout actions = new LinearLayout(context);
    actions.setOrientation(HORIZONTAL);
    addView(actions);
    addButton(actions, R.string.saved_layers_show_all, () -> setAll(true));
    addButton(actions, R.string.saved_layers_hide_all, () -> setAll(false));
    search = new EditText(context);
    search.setSingleLine(true);
    search.setHint(R.string.saved_layers_search);
    search.setContentDescription(getResources().getString(R.string.saved_layers_search));
    addView(search);
    rows = new androidx.recyclerview.widget.RecyclerView(context);
    rows.setLayoutManager(new androidx.recyclerview.widget.LinearLayoutManager(context));
    adapter.setHasStableIds(true);
    rows.setAdapter(adapter);
    addView(rows, new LayoutParams(LayoutParams.MATCH_PARENT,
        Math.min(getResources().getDisplayMetrics().heightPixels / 4, padding * 16)));
    LinearLayout footer = new LinearLayout(context);
    addView(footer);
    addButton(footer, R.string.saved_layers_manage, manage);
    addButton(footer, R.string.osmand_import_title, importPlaces);
    search.addTextChangedListener(new TextWatcher()
    {
      public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
      public void onTextChanged(CharSequence s, int start, int before, int count) { onChanged(); }
      public void afterTextChanged(Editable e) {}
    });
    onChanged();
  }

  private MaterialButton newActionButton()
  {
    MaterialButton button = new MaterialButton(getContext());
    // The app's night palette can pair a dark primary surface with dark on-primary text.
    // Choose the higher-contrast foreground for the actual button surface in either theme.
    var tint = button.getBackgroundTintList();
    if (tint != null)
      button.setTextColor(androidx.core.graphics.ColorUtils.calculateLuminance(
          tint.getColorForState(new int[]{android.R.attr.state_enabled}, tint.getDefaultColor())) > 0.179
          ? android.graphics.Color.BLACK : android.graphics.Color.WHITE);
    return button;
  }

  private void addButton(LinearLayout parent, int label, Runnable action)
  {
    MaterialButton button = newActionButton();
    button.setText(label);
    button.setOnClickListener(v -> action.run());
    button.setTextSize(14);
    parent.addView(button, new LayoutParams(0, LayoutParams.WRAP_CONTENT, 1));
  }

  private void setAll(boolean visible)
  {
    BookmarkManager.INSTANCE.setAllCategoriesVisibility(visible);
    onChanged();
  }

  @Override protected void onAttachedToWindow()
  {
    super.onAttachedToWindow();
    BookmarkManager.INSTANCE.addCategoriesUpdatesListener(this);
    onChanged();
  }

  @Override protected void onDetachedFromWindow()
  {
    BookmarkManager.INSTANCE.removeCategoriesUpdatesListener(this);
    super.onDetachedFromWindow();
  }

  @Override public void onChanged()
  {
    var categories = BookmarkManager.INSTANCE.getCategories();
    search.setVisibility(categories.size() > 6 || search.length() > 0 ? View.VISIBLE : View.GONE);
    String query = search.getText().toString().toLowerCase(Locale.ROOT);
    visibleRows.clear();
    for (BookmarkCategory category : categories)
      if (category.getName().toLowerCase(Locale.ROOT).contains(query)) visibleRows.add(category);
    int rowHeight = Math.round(72 * getResources().getDisplayMetrics().scaledDensity);
    rows.getLayoutParams().height = Math.min(getResources().getDisplayMetrics().heightPixels / 4,
        Math.max(1, visibleRows.size()) * rowHeight);
    rows.requestLayout();
    adapter.notifyDataSetChanged();
  }

  private final class PlaceHolder extends androidx.recyclerview.widget.RecyclerView.ViewHolder
  {
    final SwitchMaterial toggle;
    final android.widget.ImageView icon;
    final MaterialButton more;
    PlaceHolder(LinearLayout row)
    {
      super(row);
      row.setGravity(android.view.Gravity.CENTER_VERTICAL);
      int unit = Math.round(getResources().getDisplayMetrics().density);
      icon = new android.widget.ImageView(getContext());
      icon.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
      row.addView(icon, new LayoutParams(32 * unit, 32 * unit));
      toggle = new SwitchMaterial(getContext());
      toggle.setMinHeight(48 * unit);
      row.addView(toggle, new LayoutParams(0, LayoutParams.WRAP_CONTENT, 1));
      more = newActionButton();
      more.setText("⋮");
      more.setMinWidth(48 * unit);
      more.setMinimumWidth(48 * unit);
      more.setPadding(0, 0, 0, 0);
      row.addView(more, new LayoutParams(48 * unit, LayoutParams.WRAP_CONTENT));
    }
  }

  private final class PlacesAdapter extends androidx.recyclerview.widget.RecyclerView.Adapter<PlaceHolder>
  {
    @Override public long getItemId(int position) { return visibleRows.get(position).getId(); }
    @Override public int getItemCount() { return visibleRows.size(); }
    @Override public PlaceHolder onCreateViewHolder(android.view.ViewGroup parent, int type)
    {
      LinearLayout row = new LinearLayout(getContext());
      row.setLayoutParams(new androidx.recyclerview.widget.RecyclerView.LayoutParams(
          LayoutParams.MATCH_PARENT, LayoutParams.WRAP_CONTENT));
      return new PlaceHolder(row);
    }
    @Override public void onBindViewHolder(PlaceHolder holder, int position)
    {
      BookmarkCategory category = visibleRows.get(position);
      int[] style = app.organicmaps.sdk.bookmarks.data.OsmAndImport.nativeStyle(category.getId());
      holder.icon.setVisibility(style[0] == 0 ? View.GONE : View.VISIBLE);
      if (style[0] != 0)
      {
        holder.icon.setImageResource(new app.organicmaps.sdk.bookmarks.data.Icon(1, style[1]).getResId());
        holder.icon.setImageTintList(android.content.res.ColorStateList.valueOf(style[0]));
      }
      holder.toggle.setOnCheckedChangeListener(null);
      String counts = getResources().getQuantityString(R.plurals.saved_layers_places,
          category.getBookmarksCount(), category.getBookmarksCount());
      if (category.getTracksCount() > 0)
        counts += " · " + getResources().getQuantityString(R.plurals.saved_layers_tracks,
            category.getTracksCount(), category.getTracksCount());
      holder.toggle.setText(category.getName() + "\n" + counts);
      holder.toggle.setChecked(BookmarkManager.INSTANCE.isVisible(category.getId()));
      holder.toggle.setOnCheckedChangeListener((button, checked) -> {
        BookmarkManager.INSTANCE.setVisibility(category.getId(), checked);
        // Refresh cache observers after the current input event, preserving RecyclerView focus.
        post(() -> BookmarkManager.INSTANCE.onBookmarksChanged());
      });
      holder.more.setContentDescription(getResources().getString(R.string.saved_layers_actions, category.getName()));
      holder.more.setOnClickListener(v -> showActions(v, category));
    }
  }

  private void showActions(View anchor, BookmarkCategory category)
  {
    PopupMenu menu = new PopupMenu(getContext(), anchor);
    menu.getMenu().add(R.string.saved_layers_only).setOnMenuItemClickListener(item -> {
      // Visibility notifications refresh the manager's live category collection synchronously.
      for (BookmarkCategory other : new java.util.ArrayList<>(BookmarkManager.INSTANCE.getCategories()))
        BookmarkManager.INSTANCE.setVisibility(other.getId(), other.getId() == category.getId());
      onChanged();
      return true;
    });
    menu.getMenu().add(R.string.saved_layers_zoom).setOnMenuItemClickListener(item -> {
      BookmarkManager.INSTANCE.setVisibility(category.getId(), true);
      BookmarkManager.INSTANCE.showBookmarkCategoryOnMap(category.getId());
      dismiss.run();
      return true;
    });
    menu.show();
  }
}
