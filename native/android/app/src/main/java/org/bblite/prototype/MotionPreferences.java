package org.bblite.prototype;

import android.content.ContentResolver;
import android.provider.Settings;

final class MotionPreferences {
    private MotionPreferences() {}

    // Chromium's Android reduced-motion policy uses the animator scale, whose default is 1.
    static boolean reducedMotion(ContentResolver resolver) {
        return Settings.Global.getFloat(resolver, Settings.Global.ANIMATOR_DURATION_SCALE, 1f) == 0f;
    }
}
