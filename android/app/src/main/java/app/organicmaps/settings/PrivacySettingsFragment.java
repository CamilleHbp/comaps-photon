package app.organicmaps.settings;
import android.annotation.SuppressLint;
import android.os.Bundle;
import android.view.View;
import androidx.annotation.Keep;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.preference.Preference;
import androidx.preference.TwoStatePreference;
import app.organicmaps.MwmApplication;
import app.organicmaps.R;
import app.organicmaps.sdk.location.LocationHelper;
import app.organicmaps.sdk.search.SearchRecents;
import app.organicmaps.sdk.util.Config;

@Keep
public class PrivacySettingsFragment extends BaseXmlSettingsFragment
{
  @Override
  protected int getXmlResources()
  {
    return R.xml.prefs_privacy;
  }

  @Override
  public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState)
  {
    super.onViewCreated(view, savedInstanceState);
    initPlayServicesPrefsCallbacks();
    initSearchPrivacyPrefsCallbacks();
    initPhotonPreferences();
  }

  private void initPlayServicesPrefsCallbacks()
  {
    final Preference pref = findPreference(getString(R.string.pref_play_services));
    if (pref == null)
      return;

    if (!MwmApplication.from(requireContext())
             .getLocationProviderFactory()
             .isGoogleLocationAvailable(requireActivity().getApplicationContext()))
    {
      pref.setVisible(false);
    }
    else
    {
      ((TwoStatePreference) pref).setChecked(Config.useGoogleServices());
      pref.setOnPreferenceChangeListener(new Preference.OnPreferenceChangeListener() {
        @SuppressLint("MissingPermission")
        @Override
        public boolean onPreferenceChange(@NonNull Preference preference, Object newValue)
        {
          final LocationHelper locationHelper = MwmApplication.from(requireContext()).getLocationHelper();
          boolean oldVal = Config.useGoogleServices();
          boolean newVal = (Boolean) newValue;
          if (oldVal != newVal)
          {
            Config.setUseGoogleService(newVal);
            if (locationHelper.isActive())
            {
              locationHelper.stop();
              locationHelper.start();
            }
          }
          return true;
        }
      });
    }
  }

  private void initPhotonPreferences()
  {
    TwoStatePreference enabled = findPreference("photon_enabled");
    enabled.setChecked(Config.isPhotonEnabled());
    enabled.setOnPreferenceChangeListener((preference, value) -> {
      Config.setPhotonEnabled((Boolean) value);
      return true;
    });
    androidx.preference.EditTextPreference endpoint = findPreference("photon_endpoint");
    endpoint.setText(Config.getPhotonEndpoint());
    endpoint.setSummary(Config.getPhotonEndpoint());
    endpoint.setOnPreferenceChangeListener((preference, value) -> {
      String text = value.toString().trim();
      android.net.Uri uri = android.net.Uri.parse(text);
      // Queries and credentials belong to the request, not the configured server.
      if (!"https".equals(uri.getScheme()) || android.text.TextUtils.isEmpty(uri.getHost())
          || uri.getUserInfo() != null || uri.getQuery() != null || uri.getFragment() != null
          || text.matches(".*\\s.*"))
      {
        android.widget.Toast
            .makeText(requireContext(), R.string.photon_endpoint_invalid, android.widget.Toast.LENGTH_LONG)
            .show();
        return false;
      }
      Config.setPhotonEndpoint(text);
      endpoint.setSummary(text);
      return true;
    });
  }

  private void initSearchPrivacyPrefsCallbacks()
  {
    final Preference pref = findPreference(getString(R.string.pref_search_history));
    if (pref == null)
      return;

    final boolean isHistoryEnabled = Config.isSearchHistoryEnabled();
    ((TwoStatePreference) pref).setChecked(isHistoryEnabled);
    pref.setOnPreferenceChangeListener((preference, newValue) -> {
      boolean newVal = (Boolean) newValue;
      if (newVal != Config.isSearchHistoryEnabled())
      {
        Config.setSearchHistoryEnabled(newVal);
        if (newVal)
          SearchRecents.refresh();
        else
          SearchRecents.clear();
      }
      return true;
    });
  }
}
