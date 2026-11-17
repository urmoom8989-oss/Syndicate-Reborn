// A standard 2017 account had only the 20 base banner icons (StandardBanner1-20). The old default profile also
// handed out Founder's Pack, Influencer, Newsletter, "Other", Save the World and extra Standard banners that no one
// can earn here, so they filled the banner picker's Founder and Special pages. This strips those from an existing
// account, once, keeping the 20 base banners and any real season-reward banner (BRSeasonNN, earned from a Battle
// Pass or the Season 1 free reward).
export const BANNER_FIX_VERSION = 1;

const base = new Set(Array.from({length: 20}, (_, i) => 'standardbanner' + (i + 1)));

// Keep a banner icon only when it is one of the 20 base icons or a season-reward banner.
function keepBanner(templateId) {
  const name = String(templateId).split(':')[1]?.toLowerCase() || '';
  return base.has(name) || name.startsWith('brseason');
}

// Remove the non-base, non-earnable banner icons from wherever banners live (common_core, and profile0 for the
// older builds). Colours and every other item are left untouched. If the equipped banner was removed, it is reset
// to a base one so the locker does not point at a missing icon. Returns true if anything changed.
export function stripNonBaseBanners(profiles) {
  let changed = false;
  for (const profileId of ['common_core', 'profile0']) {
    const items = profiles?.[profileId]?.items;
    if (!items) continue;
    for (const [key, item] of Object.entries(items)) {
      const templateId = String(item?.templateId || '');
      if (templateId.toLowerCase().startsWith('homebasebannericon:') && !keepBanner(templateId)) {
        delete items[key];
        changed = true;
      }
    }
  }
  const attrs = profiles?.athena?.stats?.attributes;
  if (attrs && typeof attrs.banner_icon === 'string' && attrs.banner_icon && !keepBanner('HomebaseBannerIcon:' + attrs.banner_icon)) {
    attrs.banner_icon = 'StandardBanner1';
    changed = true;
  }
  return changed;
}
