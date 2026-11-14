#!/usr/bin/env bash
# Records how many people are finding and using GlowLang.
#
#   tools/stats.sh                 print today's numbers
#   tools/stats.sh stats.csv       also add them as a row to stats.csv
#
# Needs the GitHub CLI (gh), signed in with push access to the repos, because
# GitHub only shows clone and view counts to people who can push. A number
# that cannot be fetched (rate limit, no access) is shown as "?".
#
# Microsoft Store installs are not available here: Partner Center shows them
# under Apps and games > GlowLang > Analytics > Acquisitions. Add that number
# to the CSV by hand once a week.
set -uo pipefail

CORE=anshumali19/GlowLang-core
PKGS=anshumali19/GlowLang-packages
OWNER=anshumali19

# num ENDPOINT JQ: one number from the GitHub API, or "?" on any failure.
# (On an error gh prints the JSON error body to stdout, so check the result.)
num() {
    local out
    out=$(gh api "$1" --jq "$2" 2>/dev/null) || { echo "?"; return; }
    [[ $out =~ ^[0-9]+$ ]] && echo "$out" || echo "?"
}

# Every download of every release asset in a repo.
downloads() {
    local out
    out=$(gh api --paginate "repos/$1/releases" \
            --jq '[.[].assets[].download_count] | add // 0' 2>/dev/null) || { echo "?"; return; }
    if [[ $out =~ ^[0-9[:space:]]+$ ]]; then
        echo "$out" | awk '{ s += $1 } END { print s + 0 }'
    else
        echo "?"
    fi
}

stars=$(num "repos/$CORE" .stargazers_count)
forks=$(num "repos/$CORE" .forks_count)
watchers=$(num "repos/$CORE" .subscribers_count)
issues=$(num "repos/$CORE" .open_issues_count)

# Unique people over the last 14 days, as GitHub reports them.
clones=$(num "repos/$CORE/traffic/clones" .uniques)
views=$(num "repos/$CORE/traffic/views" .uniques)

dl_core=$(downloads "$CORE")
dl_pkgs=$(downloads "$PKGS")

# Issues and pull requests opened by someone other than the owner or a bot.
others=$(num "search/issues?q=repo:$CORE+-author:$OWNER+-author:app/dependabot" .total_count)

# Public GlowLang files by other people. Other languages use .glow too, so
# this looks for the eif keyword, which only GlowLang has.
glow_files=$(num "search/code?q=extension:glow+eif+-user:$OWNER" .total_count)

date=$(date +%Y-%m-%d)
printf '%-28s %s\n' \
    "date" "$date" \
    "stars" "$stars" \
    "forks" "$forks" \
    "watchers" "$watchers" \
    "open issues and PRs" "$issues" \
    "unique cloners (14 days)" "$clones" \
    "unique visitors (14 days)" "$views" \
    "release downloads (core)" "$dl_core" \
    "release downloads (packages)" "$dl_pkgs" \
    "issues/PRs by others" "$others" \
    "GlowLang files by others" "$glow_files"

if [ $# -ge 1 ]; then
    csv=$1
    if [ ! -f "$csv" ]; then
        echo "date,stars,forks,watchers,open_issues,cloners_14d,visitors_14d,downloads_core,downloads_packages,outside_issues,outside_glow_files,store_installs" > "$csv"
    fi
    echo "$date,$stars,$forks,$watchers,$issues,$clones,$views,$dl_core,$dl_pkgs,$others,$glow_files," >> "$csv"
    echo "added to $csv (fill in store_installs from Partner Center)"
fi
