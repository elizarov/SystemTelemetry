(function () {
  const data = window.CaseDashSite || { themes: [], defaultTheme: "" };
  const themes = data.themes || [];
  const select = document.getElementById("themeSelect");
  const sectionSelect = document.getElementById("sectionSelect");
  const brandIcon = document.getElementById("brandIcon");
  const dashboardImage = document.getElementById("dashboardImage");
  const guideImage = document.getElementById("guideImage");
  const storageKey = "casedash.theme";
  const sectionLinks = Array.from(document.querySelectorAll("[data-section-link]"));
  const sections = sectionLinks
    .map((link) => document.getElementById(link.dataset.sectionLink))
    .filter(Boolean);

  function readSavedTheme() {
    try {
      return localStorage.getItem(storageKey);
    } catch {
      return null;
    }
  }

  function writeSavedTheme(themeId) {
    try {
      localStorage.setItem(storageKey, themeId);
    } catch {
      // Some browsers restrict storage for file:// pages; theme switching still works for the session.
    }
  }

  function hexToRgb(hex) {
    const value = hex.replace("#", "");
    return {
      r: parseInt(value.slice(0, 2), 16),
      g: parseInt(value.slice(2, 4), 16),
      b: parseInt(value.slice(4, 6), 16),
      a: parseInt(value.slice(6, 8) || "ff", 16) / 255,
    };
  }

  function rgbToCss(color, alpha) {
    return `rgba(${color.r}, ${color.g}, ${color.b}, ${alpha ?? color.a})`;
  }

  function mix(a, b, amount) {
    return {
      r: Math.round(a.r + (b.r - a.r) * amount),
      g: Math.round(a.g + (b.g - a.g) * amount),
      b: Math.round(a.b + (b.b - a.b) * amount),
      a: a.a + (b.a - a.a) * amount,
    };
  }

  function contrastText(background) {
    const luminance = (0.2126 * background.r + 0.7152 * background.g + 0.0722 * background.b) / 255;
    return luminance > 0.58 ? "#000000" : "#ffffff";
  }

  function setTheme(theme) {
    if (!theme) {
      return;
    }

    const root = document.documentElement;
    const background = hexToRgb(theme.colors.background);
    const foreground = hexToRgb(theme.colors.foreground);
    const accent = hexToRgb(theme.colors.accent);
    const guide = hexToRgb(theme.colors.guide);
    const panel = mix(background, foreground, 0.08);
    const border = mix(background, foreground, 0.18);

    root.style.setProperty("--background", rgbToCss(background));
    root.style.setProperty("--foreground", rgbToCss(foreground));
    root.style.setProperty("--accent", rgbToCss(accent));
    root.style.setProperty("--guide", rgbToCss(guide));
    root.style.setProperty("--panel", rgbToCss(panel));
    root.style.setProperty("--panel-strong", rgbToCss(mix(background, foreground, 0.13)));
    root.style.setProperty("--border", rgbToCss(border));
    root.style.setProperty("--muted", rgbToCss(mix(background, foreground, 0.66)));
    root.style.setProperty("--accent-contrast", contrastText(accent));
    root.style.setProperty("--accent-soft", rgbToCss(accent, 0.18));
    root.style.setProperty("--guide-soft", rgbToCss(guide, 0.2));

    brandIcon.src = theme.assets.icon;
    dashboardImage.src = theme.assets.dashboard;
    guideImage.src = theme.assets.guide;
    dashboardImage.alt = `CaseDash dashboard using the ${theme.name} theme`;
    guideImage.alt = `CaseDash layout guide sheet using the ${theme.name} theme`;
    select.value = theme.id;
    writeSavedTheme(theme.id);
  }

  function initialThemeId() {
    const saved = readSavedTheme();
    if (themes.some((theme) => theme.id === saved)) {
      return saved;
    }
    return data.defaultTheme || (themes[0] && themes[0].id);
  }

  function setCurrentSection(id) {
    sectionLinks.forEach((link) => {
      const isCurrent = link.dataset.sectionLink === id;
      link.classList.toggle("current", isCurrent);
      if (isCurrent) {
        link.setAttribute("aria-current", "true");
      } else {
        link.removeAttribute("aria-current");
      }
    });
    if (sectionSelect && sectionSelect.value !== id) {
      sectionSelect.value = id;
    }
  }

  function updateCurrentSectionFromScroll() {
    if (sections.length === 0) {
      return;
    }

    const pageBottom = window.scrollY + window.innerHeight;
    const nearPageEnd = pageBottom >= document.documentElement.scrollHeight - 4;
    if (nearPageEnd) {
      setCurrentSection(sections[sections.length - 1].id);
      return;
    }

    const marker = window.scrollY + Math.max(120, window.innerHeight * 0.32);
    let current = sections[0];
    sections.forEach((section) => {
      if (section.offsetTop <= marker) {
        current = section;
      }
    });
    setCurrentSection(current.id);
  }

  function observeSections() {
    let frame = 0;
    const schedule = () => {
      if (frame) {
        return;
      }
      frame = window.requestAnimationFrame(() => {
        frame = 0;
        updateCurrentSectionFromScroll();
      });
    };

    window.addEventListener("scroll", schedule, { passive: true });
    window.addEventListener("resize", schedule);
    updateCurrentSectionFromScroll();
  }

  themes.forEach((theme) => {
    const option = document.createElement("option");
    option.value = theme.id;
    option.textContent = theme.name;
    if (theme.description) {
      option.title = theme.description;
    }
    select.appendChild(option);
  });

  select.addEventListener("change", () => {
    setTheme(themes.find((theme) => theme.id === select.value));
  });

  if (sectionSelect) {
    sectionSelect.addEventListener("change", () => {
      const section = document.getElementById(sectionSelect.value);
      if (section) {
        section.scrollIntoView({ behavior: "smooth", block: "start" });
        setCurrentSection(section.id);
      }
    });
  }

  setTheme(themes.find((theme) => theme.id === initialThemeId()) || themes[0]);
  setCurrentSection(sections[0] && sections[0].id);
  observeSections();
})();
