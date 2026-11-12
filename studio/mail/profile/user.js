// Studio: Mail (Thunderbird) settings. Studio copies this file into its Mail profile on
// every start, so edit it in the repo (studio/mail/profile/user.js), not in the profile.
// Accounts, passwords and everything else Thunderbird keeps in prefs.js are never touched.

// Studio's look (chrome/userChrome.css)
user_pref("toolkit.legacyUserProfileCustomizations.stylesheets", true);
user_pref("extensions.activeThemeID", "thunderbird-compact-light@mozilla.org");
user_pref("ui.systemUsesDarkTheme", 0);
user_pref("browser.theme.toolbar-theme", 1);
user_pref("browser.theme.content-theme", 1);
user_pref("layout.css.prefers-color-scheme.content-override", 1);
user_pref("mail.uidensity", 0); // compact
user_pref("mail.tabs.drawInTitlebar", false); // Studio has the window keys

// quiet start: no start page, no default-client question, no rights bar, no appeals
user_pref("mailnews.start_page.enabled", false);
user_pref("mail.shell.checkDefaultClient", false);
user_pref("mail.rights.override", true);
user_pref("mail.inappnotifications.enabled", false);
user_pref("app.donation.eoy.version.viewed", 999);

// no telemetry
user_pref("toolkit.telemetry.enabled", false);
user_pref("toolkit.telemetry.unified", false);
user_pref("toolkit.telemetry.archive.enabled", false);
user_pref("toolkit.telemetry.reportingpolicy.firstRun", false);
user_pref("datareporting.healthreport.uploadEnabled", false);
user_pref("datareporting.policy.dataSubmissionEnabled", false);
user_pref("browser.crashReports.unsubmittedCheck.autoSubmit2", false);
