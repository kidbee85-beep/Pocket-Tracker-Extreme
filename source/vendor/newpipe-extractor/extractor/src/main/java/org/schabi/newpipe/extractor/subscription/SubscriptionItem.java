package org.schabi.newpipe.extractor.subscription;

import java.io.Serializable;
import java.util.Objects;

// Android API26 compatibility: equivalent immutable value class avoids java.lang.Record
// global synthetics in this app's Java11 Android consumer. Upstream API is retained.
public final class SubscriptionItem implements Serializable {
    private static final long serialVersionUID = 0L;
    private final int serviceId;
    private final String url;
    private final String name;
    public SubscriptionItem(final int serviceId, final String url, final String name) {
        this.serviceId = serviceId; this.url = url; this.name = name;
    }
    public int serviceId() { return serviceId; }
    public String url() { return url; }
    public String name() { return name; }
    @Override public boolean equals(final Object value) {
        if (this == value) return true;
        if (!(value instanceof SubscriptionItem)) return false;
        final SubscriptionItem other = (SubscriptionItem) value;
        return serviceId == other.serviceId && Objects.equals(url, other.url) && Objects.equals(name, other.name);
    }
    @Override public int hashCode() { return (serviceId * 31 + Objects.hashCode(url)) * 31 + Objects.hashCode(name); }
    @Override public String toString() {
        return "SubscriptionItem[serviceId=" + serviceId + ", url=" + url + ", name=" + name + "]";
    }
    public static final SubscriptionItem INVALID =
            new SubscriptionItem(Integer.MIN_VALUE, "Invalid", "Invalid");
}
