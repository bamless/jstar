const LIGHT_THEME = "default";
const DARK_THEME = "blue-dark";

const THEME_ICONS = {
    auto: "fa-adjust",
    light: "fa-sun",
    dark: "fa-moon",
};

const THEME_STORAGE_KEY = "jtd-color";

function isDarkPreference(pref) {
    return pref === "dark" ||
        (pref === "auto" && window.matchMedia("(prefers-color-scheme: dark)").matches);
}

function updateThemeIcon() {
    const icon = document.getElementById("theme-icon");
    if (!icon) return;
    const pref = localStorage.getItem(THEME_STORAGE_KEY) || "auto";
    icon.className = `fas ${THEME_ICONS[pref] || THEME_ICONS.auto}`;
}

function applyTheme() {
    const pref = localStorage.getItem(THEME_STORAGE_KEY) || "auto";
    const isDark = isDarkPreference(pref);

    document.documentElement.setAttribute('data-theme', isDark ? DARK_THEME : LIGHT_THEME);
    if (typeof jtd !== 'undefined' && jtd.setTheme) {
        jtd.setTheme(isDark ? DARK_THEME : LIGHT_THEME);
    }

    const prism = document.getElementById("prism-theme");
    if (prism) {
        prism.href = isDark
            ? prism.href.replace(/prism[^/]*\.css/, "prism-dark.css")
            : prism.href.replace(/prism[^/]*\.css/, "prism.css");
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
