# SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
# SPDX-License-Identifier: Apache-2.0

function label_count(label)
{
    return (label in counts) ? counts[label] : 0
}

function label_time(label)
{
    return (label in times) ? times[label] : 0
}

function print_header()
{
    print "  -----------------------------------------------"
    printf "  %-24s %8s %10s\n", "Category", "Tests", "Time"
    print "  -----------------------------------------------"
}

function print_separator()
{
    print "  -----------------------------------------------"
}

function print_row(name, count, seconds)
{
    printf "  %-24s %8d %8.2f s\n", name, count, seconds
}

function print_report(environment_total_count, environment_total_time)
{
    if (report_printed || !labels_seen) {
        return
    }

    print_header()

    if (report == "layer") {
        print_row("Common", label_count("common"), label_time("common"))
        print_row("Adapter", label_count("adapter"), label_time("adapter"))
        print_row("Component", label_count("component"), label_time("component"))
        print_row("Core", label_count("core"), label_time("core"))
        print_separator()
        print_row("TOTAL", label_count("actrust"), label_time("actrust"))
    } else if (report == "form") {
        print_row("Unit", label_count("unit"), label_time("unit"))
        print_row("Smoke", label_count("smoke"), label_time("smoke"))
        print_separator()
        print_row("TOTAL", label_count("actrust"), label_time("actrust"))
    } else if (report == "environment") {
        print_row("Network", label_count("network"), label_time("network"))
        print_row("AWS/PKI Integration", label_count("integration"), \
            label_time("integration"))
        environment_total_count = label_count("network") + \
            label_count("integration")
        environment_total_time = label_time("network") + \
            label_time("integration")
        print_separator()
        print_row("TOTAL", environment_total_count, environment_total_time)
    }

    report_printed = 1
}

/^Label Time Summary:$/ {
    in_labels = 1
    labels_seen = 1
    next
}

in_labels && $2 == "=" && $4 == "sec*proc" {
    label = $1
    times[label] = $3 + 0
    count = $5
    gsub(/[()]/, "", count)
    counts[label] = count + 0
    next
}

in_labels && /^Total Test time/ {
    in_labels = 0
}

END {
    print_report()
}
