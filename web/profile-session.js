// A second ordinary tab can become a server-browser guest after boot. Reserve
// the normal writable profile for one engine; other tabs use their guest profile.
export async function isolateProfile({guest, locks, hold}) {
  if (guest) return true;
  if (!locks?.request) return false;
  return new Promise((resolve, reject) => {
    locks.request('kisak-primary-profile', {ifAvailable:true}, lock => {
      resolve(!lock);
      return lock ? hold : undefined;
    }).catch(reject);
  });
}
