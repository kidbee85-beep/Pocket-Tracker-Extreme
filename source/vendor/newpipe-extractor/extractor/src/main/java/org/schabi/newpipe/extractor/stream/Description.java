/*
 * SPDX-FileCopyrightText: 2026 NewPipe e.V. <https://newpipe-ev.de>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

package org.schabi.newpipe.extractor.stream;

import javax.annotation.Nonnull;
import javax.annotation.Nullable;
import java.io.Serializable;
import java.util.Objects;

import static org.schabi.newpipe.extractor.utils.Utils.isNullOrEmpty;

// Android compatibility value class; preserves the upstream record's public value API.
public final class Description implements Serializable {
    private static final long serialVersionUID = 0L;
    private final String content;
    private final Type type;
    public Description(@Nonnull final String content, @Nonnull final Type type) {
        this.content = content; this.type = type;
    }
    @Nonnull public String content() { return content; }
    @Nonnull public Type type() { return type; }
    @Override public boolean equals(final Object value) {
        if (this == value) return true;
        if (!(value instanceof Description)) return false;
        final Description other = (Description) value;
        return Objects.equals(content, other.content) && type == other.type;
    }
    @Override public int hashCode() { return Objects.hashCode(content) * 31 + Objects.hashCode(type); }
    @Override public String toString() { return "Description[content=" + content + ", type=" + type + "]"; }

    @Nonnull
    public static final Description EMPTY_DESCRIPTION = new Description("", Type.PLAIN_TEXT);

    public enum Type {
        HTML, MARKDOWN, PLAIN_TEXT
    }

    @Nonnull
    public static Description of(@Nullable final String content, @Nonnull final Type type) {
        if (isNullOrEmpty(content)) {
            return EMPTY_DESCRIPTION;
        }
        return new Description(content, type);
    }
}
