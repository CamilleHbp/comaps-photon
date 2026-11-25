import {validPoint} from './routing.js';

// Each field owns its request and selection; labels are never used as place IDs.
export function attachPlaceAutocomplete(input, {language, t, onEdit, onSelect, isSelected}) {
  const list = document.createElement('div');
  list.id = `${input.id}-suggestions`;
  list.className = 'route-suggestions';
  list.setAttribute('role', 'listbox');
  list.setAttribute('aria-label', t('routeSuggestions'));
  list.hidden = true;
  const status = document.createElement('p');
  status.className = 'route-search-status';
  status.setAttribute('role', 'status');
  const popup = document.createElement('div');
  popup.className = 'route-autocomplete-popup'; popup.hidden = true;
  popup.append(list, status); input.after(popup);
  function position() {
    if (popup.hidden) return;
    const box = input.getBoundingClientRect();
    const viewport = window.visualViewport;
    const top = viewport?.offsetTop || 0, height = viewport?.height || innerHeight;
    const below = top + height - box.bottom - 8, above = box.top - top - 8;
    const upwards = below < 180 && above > below;
    popup.style.left = `${Math.max(8, box.left)}px`;
    popup.style.width = `${Math.min(box.width, innerWidth - 16)}px`;
    popup.style.top = upwards ? 'auto' : `${box.bottom + 4}px`;
    popup.style.bottom = upwards ? `${innerHeight - box.top + 4}px` : 'auto';
    popup.style.maxHeight = `${Math.max(60, Math.min(280, upwards ? above : below))}px`;
  }
  const events = new AbortController();
  document.addEventListener('scroll', position, {capture:true, signal:events.signal});
  window.addEventListener('resize', position, {signal:events.signal});
  window.visualViewport?.addEventListener('resize', position, {signal:events.signal});
  window.visualViewport?.addEventListener('scroll', position, {signal:events.signal});
  input.setAttribute('role', 'combobox');
  input.setAttribute('aria-autocomplete', 'list');
  input.setAttribute('aria-controls', list.id);
  input.setAttribute('aria-expanded', 'false');
  input.autocomplete = 'off';
  let timer, controller, items = [], active = -1;
  function close() {
    clearTimeout(timer); controller?.abort();
    popup.hidden = true; list.hidden = true; list.replaceChildren(); items = []; active = -1;
    input.setAttribute('aria-expanded', 'false');
    input.removeAttribute('aria-activedescendant'); status.textContent = '';
  }
  function choose(index) {
    const item = items[index];
    if (!item) return;
    input.value = item.label; onSelect(item); close(); input.focus();
  }
  async function search() {
    close();
    const query = input.value.trim();
    if (query.length < 2 || /^[+\-\d.\s,]+$/.test(query)) return;
    popup.hidden = false; position();
    if (!navigator.onLine) { status.textContent = t('offlineSearch'); return; }
    const request = new AbortController(); controller = request;
    status.textContent = t('searching');
    try {
      const url = new URL('https://photon.komoot.io/api/');
      url.search = new URLSearchParams({q: query, limit: '6', lang: language});
      const response = await fetch(url, {signal: AbortSignal.any([request.signal, AbortSignal.timeout(12000)]), cache: 'no-store'});
      if (!response.ok) throw new Error(response.status === 429 ? 'busy' : 'searchError');
      const data = await response.json();
      if (request.signal.aborted || !input.isConnected || input.value.trim() !== query) return;
      items = (data.features || []).map(feature => {
        const p = feature.properties || {};
        const point = {lon: feature.geometry?.coordinates?.[0], lat: feature.geometry?.coordinates?.[1]};
        const label = [...new Set([p.name, [p.street,p.housenumber].filter(Boolean).join(' '), p.postcode, p.city||p.town||p.village, p.state, p.country].filter(Boolean))].join(', ') || query;
        return {point, label};
      }).filter(item => validPoint(item.point));
      status.textContent = items.length ? t('routeChoosePlace') : t('routePlaceMissing');
      items.forEach((item, index) => {
        const option = document.createElement('div');
        option.id = `${list.id}-${index}`; option.setAttribute('role', 'option');
        option.setAttribute('aria-selected', 'false'); option.textContent = item.label;
        option.onpointerdown = event => event.preventDefault();
        option.onclick = () => choose(index); list.append(option);
      });
      list.hidden = !items.length; position(); input.setAttribute('aria-expanded', String(!!items.length));
    } catch (error) {
      if (!request.signal.aborted && input.isConnected) status.textContent = t(error.message === 'busy' ? 'busy' : 'searchError');
    }
  }
  input.oninput = () => { close(); onEdit(input.value); timer = setTimeout(search, 300); };
  input.onkeydown = event => {
    if (event.key === 'Escape') { close(); return; }
    if (event.key === 'Enter' && active >= 0) { event.preventDefault(); choose(active); return; }
    if (!items.length || !['ArrowDown', 'ArrowUp'].includes(event.key)) return;
    event.preventDefault(); active = (active + (event.key === 'ArrowDown' ? 1 : active < 0 ? 0 : -1) + items.length) % items.length;
    [...list.children].forEach((option,index) => option.setAttribute('aria-selected', String(index === active)));
    input.setAttribute('aria-activedescendant', list.children[active].id);
    list.children[active].scrollIntoView({block: 'nearest'});
  };
  input.onfocus = () => { if (!isSelected()) timer = setTimeout(search, 300); };
  input.onblur = close;
  close.destroy = () => { close(); events.abort(); popup.remove(); };
  return close;
}
