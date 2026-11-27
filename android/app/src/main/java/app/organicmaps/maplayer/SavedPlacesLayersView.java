package app.organicmaps.maplayer;

import android.content.Context;
import android.content.res.ColorStateList;
import android.text.Editable;
import android.text.TextWatcher;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.PopupMenu;
import android.widget.TextView;
import androidx.annotation.NonNull;
import androidx.core.view.ViewCompat;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;
import app.organicmaps.R;
import app.organicmaps.sdk.bookmarks.data.BookmarkCategory;
import app.organicmaps.sdk.bookmarks.data.BookmarkManager;
import app.organicmaps.sdk.bookmarks.data.DataChangedListener;
import app.organicmaps.sdk.bookmarks.data.Icon;
import app.organicmaps.sdk.bookmarks.data.OsmAndImport;
import com.google.android.material.chip.Chip;
import com.google.android.material.chip.ChipGroup;
import com.google.android.material.color.MaterialColors;
import com.google.android.material.switchmaterial.SwitchMaterial;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/** One scrolling surface; list switches use the Favorites visibility state directly. */
final class SavedPlacesLayersView extends LinearLayout implements DataChangedListener
{
  private final RecyclerView rows;
  private final List<BookmarkCategory> visibleRows = new ArrayList<>();
  private final PlacesAdapter adapter = new PlacesAdapter();
  private final View header;
  private final EditText search;
  private final View searchContainer;
  private final TextView status;
  private final TextView empty;
  private final Runnable dismiss;
  private boolean reserveListSpace;

  SavedPlacesLayersView(Context context, List<LayerBottomSheetItem> overlays, Runnable manage,
                        Runnable dismiss, Runnable importPlaces)
  {
    super(context);
    this.dismiss = dismiss;
    setOrientation(VERTICAL);
    LayoutInflater.from(context).inflate(R.layout.fragment_toggle_map_layer, this, true);
    findViewById(R.id.close_button).setOnClickListener(v -> dismiss.run());
    findViewById(R.id.layer_options).setOnClickListener(v -> {
      PopupMenu menu = new PopupMenu(context, v);
      menu.getMenu().add(R.string.saved_layers_show_all).setOnMenuItemClickListener(item -> {
        BookmarkManager.INSTANCE.setAllCategoriesVisibility(true); return true;
      });
      menu.getMenu().add(R.string.saved_layers_hide_all).setOnMenuItemClickListener(item -> {
        BookmarkManager.INSTANCE.setAllCategoriesVisibility(false); return true;
      });
      menu.getMenu().add(R.string.saved_layers_manage).setOnMenuItemClickListener(item -> {
        manage.run(); return true;
      });
      menu.getMenu().add(R.string.osmand_import_title).setOnMenuItemClickListener(item -> {
        importPlaces.run(); return true;
      });
      menu.show();
    });
    rows = findViewById(R.id.recycler);
    rows.setLayoutManager(new LinearLayoutManager(context));
    rows.setItemAnimator(null);
    header = LayoutInflater.from(context).inflate(R.layout.item_layers_header, rows, false);
    ChipGroup chips = header.findViewById(R.id.layer_overlays);
    for (LayerBottomSheetItem overlay : overlays)
    {
      Chip chip = (Chip) LayoutInflater.from(context).inflate(R.layout.item_layer_filter, chips, false);
      chip.setText(overlay.getTitle());
      chip.setChecked(overlay.getMode().isEnabled(context));
      chip.setOnClickListener(v -> {
        overlay.onClick(v, overlay);
        chip.setChecked(overlay.getMode().isEnabled(context));
      });
      chips.addView(chip);
    }
    search = header.findViewById(R.id.layer_search);
    searchContainer = header.findViewById(R.id.layer_search_container);
    status = header.findViewById(R.id.layer_visibility_summary);
    empty = header.findViewById(R.id.layer_empty);
    header.findViewById(R.id.layer_search_clear).setOnClickListener(v -> search.setText(""));
    ViewCompat.setAccessibilityHeading(header.findViewById(R.id.saved_places_heading), true);
    adapter.setHasStableIds(true);
    rows.setAdapter(adapter);
    search.addTextChangedListener(new TextWatcher()
    {
      public void beforeTextChanged(CharSequence s, int start, int count, int after) {}
      public void onTextChanged(CharSequence s, int start, int before, int count) { onChanged(); }
      public void afterTextChanged(Editable e) {}
    });
    onChanged();
  }

  @Override protected void onMeasure(int width, int height)
  {
    // Bound the entire surface, not an inner list. All content shares this scrolling viewport.
    int limit = Math.round(getResources().getDisplayMetrics().heightPixels * 0.76f);
    if (MeasureSpec.getMode(height) != MeasureSpec.UNSPECIFIED)
      limit = Math.min(limit, MeasureSpec.getSize(height));
    // Keep the search field stationary as its results narrow, including an empty result.
    super.onMeasure(width, MeasureSpec.makeMeasureSpec(limit,
        reserveListSpace ? MeasureSpec.EXACTLY : MeasureSpec.AT_MOST));
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
    if (rows.isComputingLayout()) { post(this::onChanged); return; }
    var categories = BookmarkManager.INSTANCE.getCategories();
    reserveListSpace = categories.size() > 6;
    String query = search.getText().toString().trim().toLowerCase(Locale.ROOT);
    List<BookmarkCategory> filtered = new ArrayList<>();
    int visible = 0;
    for (BookmarkCategory category : categories)
    {
      if (BookmarkManager.INSTANCE.isVisible(category.getId())) ++visible;
      if (category.getName().toLowerCase(Locale.ROOT).contains(query)) filtered.add(category);
    }
    status.setText(getResources().getString(R.string.saved_layers_visible, visible, categories.size()));
    searchContainer.setVisibility(categories.size() > 6 || search.length() > 0 ? VISIBLE : GONE);
    header.findViewById(R.id.layer_search_clear).setVisibility(search.length() > 0 ? VISIBLE : INVISIBLE);
    empty.setText(categories.isEmpty() ? R.string.saved_layers_empty : R.string.saved_layers_no_results);
    empty.setVisibility(filtered.isEmpty() ? VISIBLE : GONE);
    boolean sameOrder = filtered.size() == visibleRows.size();
    for (int i = 0; sameOrder && i < filtered.size(); ++i)
      sameOrder = filtered.get(i).getId() == visibleRows.get(i).getId();
    visibleRows.clear();
    visibleRows.addAll(filtered);
    if (sameOrder) adapter.notifyItemRangeChanged(1, visibleRows.size());
    else adapter.notifyDataSetChanged();
  }

  private final class PlaceHolder extends RecyclerView.ViewHolder
  {
    final SwitchMaterial toggle;
    final ImageView icon;
    final TextView name;
    final TextView count;
    final View more;
    PlaceHolder(View row)
    {
      super(row);
      toggle = row.findViewById(R.id.layer_visible);
      icon = row.findViewById(R.id.layer_icon);
      name = row.findViewById(R.id.layer_name);
      count = row.findViewById(R.id.layer_count);
      more = row.findViewById(R.id.layer_more);
      row.setOnClickListener(v -> toggle.toggle());
    }
  }

  private final class PlacesAdapter extends RecyclerView.Adapter<RecyclerView.ViewHolder>
  {
    @Override public long getItemId(int position) { return position == 0 ? Long.MIN_VALUE : visibleRows.get(position - 1).getId(); }
    @Override public int getItemCount() { return visibleRows.size() + 1; }
    @Override public int getItemViewType(int position) { return position == 0 ? 0 : 1; }
    @NonNull @Override public RecyclerView.ViewHolder onCreateViewHolder(@NonNull ViewGroup parent, int type)
    {
      if (type == 0) return new RecyclerView.ViewHolder(header) {};
      return new PlaceHolder(LayoutInflater.from(getContext()).inflate(R.layout.item_saved_place_layer, parent, false));
    }
    @Override public void onBindViewHolder(@NonNull RecyclerView.ViewHolder item, int position)
    {
      if (position == 0) return;
      PlaceHolder holder = (PlaceHolder) item;
      BookmarkCategory category = visibleRows.get(position - 1);
      int[] style = OsmAndImport.nativeStyle(category.getId());
      holder.icon.setImageResource(new Icon(1, style[1]).getResId());
      holder.icon.setImageTintList(ColorStateList.valueOf(style[0] != 0 ? style[0]
          : MaterialColors.getColor(holder.icon, android.R.attr.textColorSecondary)));
      String counts = getResources().getQuantityString(R.plurals.saved_layers_places,
          category.getBookmarksCount(), category.getBookmarksCount());
      if (category.getTracksCount() > 0)
        counts += " · " + getResources().getQuantityString(R.plurals.saved_layers_tracks,
            category.getTracksCount(), category.getTracksCount());
      holder.name.setText(category.getName());
      holder.count.setText(counts);
      holder.toggle.setContentDescription(category.getName() + ", " + counts);
      holder.toggle.setOnCheckedChangeListener(null);
      holder.toggle.setChecked(BookmarkManager.INSTANCE.isVisible(category.getId()));
      holder.toggle.setOnCheckedChangeListener((button, checked) -> {
        BookmarkManager.INSTANCE.setVisibility(category.getId(), checked);
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
      // Batch the shared visibility update instead of rebuilding the category cache for every list.
      BookmarkManager.INSTANCE.setAllCategoriesVisibility(false);
      BookmarkManager.INSTANCE.setVisibility(category.getId(), true);
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
