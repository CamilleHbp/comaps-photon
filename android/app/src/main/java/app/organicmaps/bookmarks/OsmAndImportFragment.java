package app.organicmaps.bookmarks;

import android.content.Intent;
import android.content.res.ColorStateList;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import androidx.lifecycle.ViewModelProvider;
import app.organicmaps.MwmActivity;
import app.organicmaps.R;
import app.organicmaps.sdk.bookmarks.data.Icon;
import com.google.android.material.button.MaterialButton;
import com.google.android.material.checkbox.MaterialCheckBox;
import org.json.JSONObject;
import java.util.ArrayList;

public class OsmAndImportFragment extends Fragment
{
  private OsmAndImportModel model;
  private LinearLayout body;
  private final ActivityResultLauncher<String[]> picker = registerForActivityResult(
      new ActivityResultContracts.OpenMultipleDocuments(), uris -> {
        if (!uris.isEmpty()) model.prepare(uris);
      });

  @Nullable @Override public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container,
                                              @Nullable Bundle state)
  {
    ScrollView scroll = new ScrollView(requireContext());
    scroll.setFillViewport(true);
    body = new LinearLayout(requireContext());
    body.setOrientation(LinearLayout.VERTICAL);
    int pad = Math.round(16 * getResources().getDisplayMetrics().density);
    body.setPadding(pad, pad, pad, pad);
    scroll.addView(body);
    return scroll;
  }
  @Override public void onViewCreated(@NonNull View view, @Nullable Bundle state)
  {
    model = new ViewModelProvider(requireActivity()).get(OsmAndImportModel.class);
    model.changes.observe(getViewLifecycleOwner(), unused -> render());
    ArrayList<Uri> documents = requireActivity().getIntent().getParcelableArrayListExtra("documents");
    if (!model.started && documents != null) model.prepare(documents);
  }
  private void text(String message)
  {
    TextView label = new TextView(requireContext());
    label.setText(message);
    label.setTextSize(16);
    label.setPadding(0, 12, 0, 12);
    body.addView(label);
  }
  private MaterialButton newActionButton()
  {
    MaterialButton button = new MaterialButton(requireContext());
    // The app's night palette can pair a dark primary surface with dark on-primary text.
    // Choose the higher-contrast foreground for the actual button surface in either theme.
    var tint = button.getBackgroundTintList();
    if (tint != null)
      button.setTextColor(androidx.core.graphics.ColorUtils.calculateLuminance(
          tint.getColorForState(new int[]{android.R.attr.state_enabled}, tint.getDefaultColor())) > 0.179
          ? android.graphics.Color.BLACK : android.graphics.Color.WHITE);
    return button;
  }

  private MaterialButton button(int label, Runnable action)
  {
    MaterialButton button = newActionButton();
    button.setText(label);
    button.setOnClickListener(v -> action.run());
    body.addView(button);
    return button;
  }
  private void render()
  {
    body.removeAllViews();
    if (model.busy)
    {
      body.addView(new ProgressBar(requireContext()));
      text(getString(R.string.osmand_import_working));
      return;
    }
    if (model.finished)
    {
      text(getString(R.string.osmand_import_result, model.added, model.duplicates, model.skipped));
      if (!model.failures.isEmpty())
      {
        text(getString(R.string.osmand_import_failures, String.join(", ", model.failures)));
        button(R.string.osmand_import_retry, model::commit);
      }
      button(R.string.osmand_import_view_layers, () -> {
        startActivity(new Intent(requireContext(), MwmActivity.class)
            .addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP)
            .putExtra("show_saved_layers", true));
        requireActivity().finish();
      });
      button(R.string.osmand_import_done, () -> requireActivity().finish());
      return;
    }
    text(getString(R.string.osmand_import_help));
    button(R.string.osmand_import_choose, () -> picker.launch(new String[]{"*/*"}));
    if (model.error != null) text(model.error);
    if (model.groups.length() > 0 && model.error == null)
    {
      text(getString(R.string.osmand_import_preview, model.skipped));
      MaterialButton importButton = newActionButton();
      importButton.setText(R.string.osmand_import_selected);
      for (int i = 0; i < model.groups.length(); ++i)
      {
        final int index = i;
        JSONObject group = model.groups.optJSONObject(i);
        LinearLayout row = new LinearLayout(requireContext());
        row.setGravity(android.view.Gravity.CENTER_VERTICAL);
        ImageView icon = new ImageView(requireContext());
        icon.setImageResource(new Icon(1, group.optInt("icon")).getResId());
        int color = group.optInt("argb", 0);
        if (color == 0) color = com.google.android.material.color.MaterialColors.getColor(
            body, com.google.android.material.R.attr.colorOnSurface);
        icon.setImageTintList(ColorStateList.valueOf(color));
        icon.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_NO);
        int size = Math.round(32 * getResources().getDisplayMetrics().density);
        row.addView(icon, new LinearLayout.LayoutParams(size, size));
        MaterialCheckBox check = new MaterialCheckBox(requireContext());
        check.setText(getString(R.string.osmand_import_group, group.optString("name"),
            group.optInt("count"), group.optInt("duplicates")));
        check.setChecked(model.selected[i]);
        check.setOnCheckedChangeListener((v, selected) -> {
          model.selected[index] = selected;
          importButton.setEnabled(anySelected());
        });
        row.addView(check, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        body.addView(row);
      }
      importButton.setEnabled(anySelected());
      importButton.setOnClickListener(v -> model.commit());
      body.addView(importButton);
    }
    button(android.R.string.cancel, () -> requireActivity().finish());
  }
  private boolean anySelected()
  {
    for (boolean selected : model.selected) if (selected) return true;
    return false;
  }
}
