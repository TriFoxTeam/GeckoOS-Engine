/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.ext

import androidx.compose.ui.graphics.Color
import androidx.core.graphics.toColorInt
import androidx.core.net.toUri
import mozilla.components.support.ktx.android.net.hostWithoutCommonPrefixes
import mozilla.components.support.ktx.util.URLStringUtils
import kotlin.text.append

// Extension functions for the String class

/**
 * Beautify a URL by truncating it in a way that highlights important parts of the URL.
 *
 * Spec: https://github.com/mozilla-mobile/focus-android/issues/1231#issuecomment-326237077
 */
fun String.beautifyUrl(): String {
    if (isNullOrEmpty() || !URLStringUtils.isHttpOrHttps(this)) {
        return this
    }

    val beautifulUrl = StringBuilder()

    val uri = this.toUri()

    // Use only the truncated host name

    val truncatedHost = uri.truncatedHost()
    if (truncatedHost.isNullOrEmpty()) {
        return this
    }

    beautifulUrl.append(truncatedHost)

    // Append the truncated path

    val truncatedPath = uri.truncatedPath()
    if (truncatedPath.isNotEmpty()) {
        beautifulUrl.append(truncatedPath)
    }

    // And then append (only) the first query parameter

    val query = uri.query
    query?.takeIf { it.isNotEmpty() }?.let {
        beautifulUrl.append("?").append(it.split("&").first())
    }

    // We always append a fragment if there's one

    uri.fragment?.takeIf { it.isNotEmpty() }?.let {
        beautifulUrl.append("#").append(it)
    }

    return beautifulUrl.toString()
}

/**
 * Tries to parse and get root domain part if [String] is a valid URL.
 */
val String.tryGetRootDomain: String
    get() =
        this.toUri().hostWithoutCommonPrefixes?.replaceAfter(".", "")?.removeSuffix(".")
            ?.replaceFirstChar { it.uppercase() } ?: this

/**
 * Tries to parse a color string and return a [Color]
 */
val String.color: Color
    get() = Color(this.toColorInt())
