const THEME_STORAGE_KEY = "jtd-color";

const LIGHT_THEME = "default";
const DARK_THEME = "blue-dark";

const LIGHT_PRISM = "prism";
const DARK_PRISM = "prism-dark";

const THEME_ICONS = {
    auto: "fa-adjust",
    light: "fa-sun",
    dark: "fa-moon",
};

function isDarkPreference(pref) {
    return pref === "dark" ||
        (pref === "auto" && window.matchMedia("(prefers-color-scheme: dark)").matches);
}

function themeForPref(pref) {
    return isDarkPreference(pref) ? DARK_THEME : LIGHT_THEME;
}

function prismForPref(pref) {
    return isDarkPreference(pref) ? DARK_PRISM : LIGHT_PRISM;
}

function updateThemeIcon() {
    const icon = document.getElementById("theme-icon");
    if (!icon) return;
    const pref = localStorage.getItem(THEME_STORAGE_KEY) || "auto";
    icon.className = `fas ${THEME_ICONS[pref] || THEME_ICONS.auto}`;
}

function applyTheme() {
    const pref = localStorage.getItem(THEME_STORAGE_KEY) || "auto";

    const theme = document.getElementById('jtd-theme-stylesheet')
    theme.href = theme.href.replace(/(just-the-docs-)[^/]*.css/, `$1${themeForPref(pref)}.css`);
    jtd.setTheme(themeForPref(pref));

    const prism = document.getElementById("prism-theme");
    if (prism) {
        prism.href = prism.href.replace(/prism[^/]*\.css/, `${prismForPref(pref)}.css`);
    }

    updateThemeIcon();
}

function toggleThemeDropdown() {
    const menu = document.getElementById("theme-menu");
    if (!menu) return;
    menu.style.display = menu.style.display === "none" ? "block" : "none";
}

function closeThemeDropdown() {
    const menu = document.getElementById("theme-menu");
    if (menu) menu.style.display = "none";
}

function selectTheme(pref) {
    localStorage.setItem(THEME_STORAGE_KEY, pref);
    closeThemeDropdown();
    applyTheme();
}

// Synchronously set the theme.
// This is a workaround replacing `jtd.setTheme` which causes a flash of unstyled content on theme switch.
const pref = localStorage.getItem(THEME_STORAGE_KEY) || "auto";
document.write('<link rel="stylesheet" id="jtd-theme-stylesheet" href="/jstar/assets/css/just-the-docs-'
    + themeForPref(pref) + '.css">');
applyTheme();

// Sync the icon once the header is in the DOM, and wire up outside-click dismissal of the dropdown.
document.addEventListener("DOMContentLoaded", () => {
    updateThemeIcon();
    document.addEventListener("click", (e) => {
        const dropdown = document.getElementById("theme-dropdown");
        if (dropdown && !dropdown.contains(e.target)) {
            closeThemeDropdown();
        }
    });
});
