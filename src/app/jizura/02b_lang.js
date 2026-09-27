(() => {
  "use strict";
  J.LANGS = ["auto", "ja", "zh-Hant", "zh-Hans", "ko", "en"];
  J.LANG_LABEL = { auto: "\u81EA\u52D5\u5224\u5B9A", ja: "\u65E5\u672C\u8A9E", "zh-Hant": "\u7E41\u9AD4\u4E2D\u6587", "zh-Hans": "\u7B80\u4F53\u4E2D\u6587", ko: "\uD55C\uAD6D\uC5B4", en: "English" };
  const TC = "\u5011\u500B\u8AAA\u9019\u6703\u5C0D\u6642\u4F86\u9084\u5F8C\u904E\u570B\u958B\u95DC\u8207\u70BA\u5F9E\u554F\u9593\u898B\u9577\u6771\u8ECA\u9580\u611B\u807D\u5B78\u8B93\u8A71\u865F\u767C\u9EDE\u7121\u73FE\u9AD4\u7D93\u96FB\u5BE6\u6A23\u8072\u8B8A\u96E2\u6C23\u5922\u7D66\u89BA\u7576\u6B61\u967D\u6200\u908A\u982D\u6DDA\u8AB0\u6B72\u9060\u55CE\u842C\u96E3\u5BEB\u61C9\u8B80\u61B6\u6A02\u9EBC\u9E97\u50B7\u5C07\u7E3D\u7D50\u7D42\u7D05\u7DA0\u7DDA\u984F\u98A8\u98DB\u9CE5\u8B1D\u8A9E\u8ACB\u8A8D\u8B58\u71B1\u71C8\u9858\u7368\u5920\u7D00\u5E36\u6EFF\u975C\u8F15\u5225\u8166\u81C9\u61F7\u8B0A\u932F\u9846\u9663\u5834\u8B9A\u6DFA\u6EAB\u8A18\u6191\u8B77\u58DE\u6B78\u5ABD\u96A8\u9280\u805E\u614B\u865B\u9059";
  const SC = "\u4EEC\u4E2A\u8BF4\u8FD9\u4F1A\u5BF9\u65F6\u6765\u8FD8\u540E\u8FC7\u56FD\u5F00\u5173\u4E0E\u4E3A\u4ECE\u95EE\u95F4\u89C1\u957F\u4E1C\u8F66\u95E8\u7231\u542C\u5B66\u8BA9\u8BDD\u53F7\u53D1\u70B9\u65E0\u73B0\u4F53\u7ECF\u7535\u5B9E\u6837\u58F0\u53D8\u79BB\u6C14\u68A6\u7ED9\u89C9\u5F53\u6B22\u9633\u604B\u8FB9\u5934\u6CEA\u8C01\u5C81\u8FDC\u5417\u4E07\u96BE\u5199\u5E94\u8BFB\u5FC6\u4E50\u4E48\u4E3D\u4F24\u5C06\u603B\u7ED3\u7EC8\u7EA2\u7EFF\u7EBF\u989C\u98CE\u98DE\u9E1F\u8C22\u8BED\u8BF7\u8BA4\u8BC6\u70ED\u706F\u613F\u72EC\u591F\u7EAA\u5E26\u6EE1\u9759\u8F7B\u522B\u8111\u8138\u6000\u8C0E\u9519\u9897\u9635\u573A\u8D5E\u6D45\u6E29\u8BB0\u51ED\u62A4\u574F\u5F52\u5988\u968F\u94F6\u95FB\u6001\u865A\u9065";
  const TCSET = /* @__PURE__ */ new Set([...TC]), SCSET = /* @__PURE__ */ new Set([...SC]);
  J.detectLang = (text) => {
    let kana = 0, hangul = 0, han = 0, tc = 0, sc = 0, latin = 0;
    for (const c of String(text || "")) {
      const u = c.codePointAt(0);
      if (u >= 65 && u <= 90 || u >= 97 && u <= 122 || u >= 192 && u <= 591 && u !== 215 && u !== 247 || u >= 65313 && u <= 65370 && (u <= 65338 || u >= 65345)) latin++;
      else if (u >= 12353 && u <= 12543 && u !== 12539 && u !== 12540 || u >= 65382 && u <= 65437) kana++;
      else if (u >= 44032 && u <= 55203 || u >= 4352 && u <= 4607 || u >= 12592 && u <= 12687) hangul++;
      else if (u >= 19968 && u <= 40959 || u >= 13312 && u <= 19903 || u >= 131072 && u <= 196607) {
        han++;
        if (TCSET.has(c)) tc++;
        if (SCSET.has(c)) sc++;
      }
    }
    const cjk = kana + hangul + han;
    if (latin >= 6 && latin >= (latin + cjk * 3) * 0.9) return "en";
    if (hangul >= 2 && hangul > kana) return "ko";
    if (kana >= 2 || kana > 0 && kana >= han * 0.03) return "ja";
    if (han >= 2) return sc > tc ? "zh-Hans" : "zh-Hant";
    return "ja";
  };
  J.resolveLang = (project) => {
    const l = project && project.lang;
    if (l && l !== "auto" && J.LANG_LABEL[l]) return l;
    return J.detectLang((project && project.lyrics || "") + " " + (project && project.title || ""));
  };
  const F = (family, weight, gf) => ({ family, weight, gf });
  const NSTC = "Noto+Sans+TC:wght@300;500;700;900", NSRTC = "Noto+Serif+TC:wght@300;500;700;800;900";
  const NSSC = "Noto+Sans+SC:wght@300;500;700;900", NSRSC = "Noto+Serif+SC:wght@300;500;700;800;900";
  const NSKR = "Noto+Sans+KR:wght@300;500;700;900", NSRKR = "Noto+Serif+KR:wght@300;500;700;800;900";
  J.LANG_FACES = {
    "zh-Hant": {
      sans: F("Noto Sans TC", 500, NSTC),
      serif: F("Noto Serif TC", 500, NSRTC),
      fbSans: '"Noto Sans TC","Noto Sans CJK TC","PingFang TC","Microsoft JhengHei"',
      fbSerif: '"Noto Serif TC","Noto Serif CJK TC","PMingLiU"',
      map: {
        gothic_black: F("Noto Sans TC", 900, NSTC),
        gothic_bold: F("Noto Sans TC", 700, NSTC),
        gothic_med: F("Noto Sans TC", 500, NSTC),
        gothic_light: F("Noto Sans TC", 300, NSTC),
        zenkaku: F("Noto Sans TC", 900, NSTC),
        sansui: F("Noto Sans TC", 500, NSTC),
        mincho_black: F("Noto Serif TC", 900, NSRTC),
        mincho_bold: F("Noto Serif TC", 700, NSRTC),
        mincho: F("Noto Serif TC", 500, NSRTC),
        mincho_light: F("Noto Serif TC", 300, NSRTC),
        tokumin: F("Noto Serif TC", 800, NSRTC),
        shippori: F("Noto Serif TC", 800, NSRTC),
        dela: F("WDXL Lubrifont TC", 400, "WDXL+Lubrifont+TC"),
        round: F("Chiron GoRound TC", 800, "Chiron+GoRound+TC:wght@800"),
        pop: F("Huninn", 400, "Huninn"),
        kiwi: F("Huninn", 400, "Huninn"),
        klee: F("LXGW WenKai TC", 700, "LXGW+WenKai+TC:wght@700"),
        brush: F("LXGW WenKai TC", 700, "LXGW+WenKai+TC:wght@700"),
        reggae: F("LXGW Marker Gothic", 400, "LXGW+Marker+Gothic"),
        rampart: F("LXGW Marker Gothic", 400, "LXGW+Marker+Gothic"),
        potta: F("LXGW Marker Gothic", 400, "LXGW+Marker+Gothic")
      }
    },
    "zh-Hans": {
      sans: F("Noto Sans SC", 500, NSSC),
      serif: F("Noto Serif SC", 500, NSRSC),
      fbSans: '"Noto Sans SC","Noto Sans CJK SC","PingFang SC","Microsoft YaHei"',
      fbSerif: '"Noto Serif SC","Noto Serif CJK SC","SimSun"',
      map: {
        gothic_black: F("Noto Sans SC", 900, NSSC),
        gothic_bold: F("Noto Sans SC", 700, NSSC),
        gothic_med: F("Noto Sans SC", 500, NSSC),
        gothic_light: F("Noto Sans SC", 300, NSSC),
        zenkaku: F("Noto Sans SC", 900, NSSC),
        sansui: F("Noto Sans SC", 500, NSSC),
        mincho_black: F("Noto Serif SC", 900, NSRSC),
        mincho_bold: F("Noto Serif SC", 700, NSRSC),
        mincho: F("Noto Serif SC", 500, NSRSC),
        mincho_light: F("Noto Serif SC", 300, NSRSC),
        tokumin: F("Noto Serif SC", 800, NSRSC),
        shippori: F("Noto Serif SC", 800, NSRSC),
        dela: F("ZCOOL QingKe HuangYou", 400, "ZCOOL+QingKe+HuangYou"),
        round: F("ZCOOL KuaiLe", 400, "ZCOOL+KuaiLe"),
        pop: F("ZCOOL KuaiLe", 400, "ZCOOL+KuaiLe"),
        kiwi: F("ZCOOL KuaiLe", 400, "ZCOOL+KuaiLe"),
        klee: F("ZCOOL XiaoWei", 400, "ZCOOL+XiaoWei"),
        brush: F("Ma Shan Zheng", 400, "Ma+Shan+Zheng"),
        reggae: F("ZCOOL QingKe HuangYou", 400, "ZCOOL+QingKe+HuangYou"),
        rampart: F("ZCOOL QingKe HuangYou", 400, "ZCOOL+QingKe+HuangYou"),
        potta: F("Ma Shan Zheng", 400, "Ma+Shan+Zheng")
      }
    },
    ko: {
      sans: F("Noto Sans KR", 500, NSKR),
      serif: F("Noto Serif KR", 500, NSRKR),
      fbSans: '"Noto Sans KR","Noto Sans CJK KR","Apple SD Gothic Neo","Malgun Gothic"',
      fbSerif: '"Noto Serif KR","Noto Serif CJK KR","AppleMyungjo","Batang"',
      map: {
        gothic_black: F("Noto Sans KR", 900, NSKR),
        gothic_bold: F("Noto Sans KR", 700, NSKR),
        gothic_med: F("Noto Sans KR", 500, NSKR),
        gothic_light: F("Noto Sans KR", 300, NSKR),
        zenkaku: F("Noto Sans KR", 900, NSKR),
        sansui: F("IBM Plex Sans KR", 500, "IBM+Plex+Sans+KR:wght@500"),
        mincho_black: F("Noto Serif KR", 900, NSRKR),
        mincho_bold: F("Noto Serif KR", 700, NSRKR),
        mincho: F("Noto Serif KR", 500, NSRKR),
        mincho_light: F("Noto Serif KR", 300, NSRKR),
        tokumin: F("Noto Serif KR", 800, NSRKR),
        shippori: F("Noto Serif KR", 800, NSRKR),
        dela: F("Black Han Sans", 400, "Black+Han+Sans"),
        round: F("Jua", 400, "Jua"),
        pop: F("Do Hyeon", 400, "Do+Hyeon"),
        kiwi: F("Gowun Dodum", 400, "Gowun+Dodum"),
        klee: F("Gowun Batang", 700, "Gowun+Batang:wght@700"),
        brush: F("Nanum Brush Script", 400, "Nanum+Brush+Script"),
        reggae: F("Black Han Sans", 400, "Black+Han+Sans"),
        rampart: F("Black Han Sans", 400, "Black+Han+Sans"),
        potta: F("Nanum Brush Script", 400, "Nanum+Brush+Script")
      }
    }
  };
  J.lang = "ja";
  J.setLang = (l) => {
    l = J.LANG_FACES[l] || l === "en" ? l : "ja";
    if (l === J.lang) return;
    J.lang = l;
    if (J.glyphs) J.glyphs.clear();
    if (J.metrics) J.metrics.clear();
  };
  const ZH_T = "\u7684\u4E00\u662F\u4E0D\u4E86\u4EBA\u6211\u5728\u6709\u4ED6\u9019\u4E2D\u5927\u4F86\u4E0A\u570B\u500B\u5230\u8AAA\u5011\u70BA\u5B50\u548C\u4F60\u5730\u51FA\u9053\u4E5F\u6642\u5E74\u5F97\u5C31\u90A3\u8981\u4E0B\u4EE5\u751F\u6703\u81EA\u8457\u53BB\u4E4B\u904E\u5BB6\u5B78\u5C0D\u53EF\u5979\u88E1\u5F8C\u5C0F\u9EBC\u5FC3\u591A\u5929\u800C\u80FD\u597D\u90FD\u7136\u6C92\u65E5\u65BC\u8D77\u9084\u767C\u6210\u4E8B\u53EA\u4F5C\u7576\u60F3\u770B\u6587\u7121\u958B\u624B\u5341\u7528\u4E3B\u884C\u65B9\u53C8\u5982\u524D\u6240\u672C\u898B\u7D93\u982D\u9762\u516C\u540C\u4E09\u5DF2\u8001\u5F9E\u52D5\u5169\u9577\u77E5\u6C11\u6A23\u73FE\u5206\u5C07\u5916\u4F46\u8EAB\u4E9B\u8207\u9AD8\u610F\u9032\u628A\u6CD5\u6B64\u5BE6\u56DE\u4E8C\u7406\u7F8E\u9EDE\u6708\u660E\u5176\u7A2E\u8072\u5168\u5DE5\u5DF1\u8A71\u5152\u8005\u5411\u60C5\u90E8\u6B63\u540D\u5B9A\u5973\u554F\u529B\u6A5F\u7D66\u7B49\u5E7E\u5F88\u6700\u9593\u65B0\u4EC0\u6253\u4FBF\u4F4D\u56E0\u91CD\u88AB\u8D70\u96FB\u56DB\u7B2C\u9580\u76F8\u6B21\u6771\u6D77\u53E3\u4F7F\u897F\u518D\u5E73\u771F\u807D\u4E16\u6C23\u4FE1\u5317\u5C11\u95DC\u611B\u5922\u5149\u5F71\u7A7A\u591C\u661F\u96E8\u6DDA\u6200\u82B1\u98A8";
  const ZH_S = "\u7684\u4E00\u662F\u4E0D\u4E86\u4EBA\u6211\u5728\u6709\u4ED6\u8FD9\u4E2D\u5927\u6765\u4E0A\u56FD\u4E2A\u5230\u8BF4\u4EEC\u4E3A\u5B50\u548C\u4F60\u5730\u51FA\u9053\u4E5F\u65F6\u5E74\u5F97\u5C31\u90A3\u8981\u4E0B\u4EE5\u751F\u4F1A\u81EA\u7740\u53BB\u4E4B\u8FC7\u5BB6\u5B66\u5BF9\u53EF\u5979\u91CC\u540E\u5C0F\u4E48\u5FC3\u591A\u5929\u800C\u80FD\u597D\u90FD\u7136\u6CA1\u65E5\u4E8E\u8D77\u8FD8\u53D1\u6210\u4E8B\u53EA\u4F5C\u5F53\u60F3\u770B\u6587\u65E0\u5F00\u624B\u5341\u7528\u4E3B\u884C\u65B9\u53C8\u5982\u524D\u6240\u672C\u89C1\u7ECF\u5934\u9762\u516C\u540C\u4E09\u5DF2\u8001\u4ECE\u52A8\u4E24\u957F\u77E5\u6C11\u6837\u73B0\u5206\u5C06\u5916\u4F46\u8EAB\u4E9B\u4E0E\u9AD8\u610F\u8FDB\u628A\u6CD5\u6B64\u5B9E\u56DE\u4E8C\u7406\u7F8E\u70B9\u6708\u660E\u5176\u79CD\u58F0\u5168\u5DE5\u5DF1\u8BDD\u513F\u8005\u5411\u60C5\u90E8\u6B63\u540D\u5B9A\u5973\u95EE\u529B\u673A\u7ED9\u7B49\u51E0\u5F88\u6700\u95F4\u65B0\u4EC0\u6253\u4FBF\u4F4D\u56E0\u91CD\u88AB\u8D70\u7535\u56DB\u7B2C\u95E8\u76F8\u6B21\u4E1C\u6D77\u53E3\u4F7F\u897F\u518D\u5E73\u771F\u542C\u4E16\u6C14\u4FE1\u5317\u5C11\u5173\u7231\u68A6\u5149\u5F71\u7A7A\u591C\u661F\u96E8\u6CEA\u604B\u82B1\u98CE";
  const KO = "\uAC00\uB098\uB2E4\uB77C\uB9C8\uBC14\uC0AC\uC544\uC790\uCC28\uCE74\uD0C0\uD30C\uD558\uAC70\uB108\uB354\uB7EC\uBA38\uBC84\uC11C\uC5B4\uC800\uCC98\uCEE4\uD130\uD37C\uD5C8\uACE0\uB178\uB3C4\uB85C\uBAA8\uBCF4\uC18C\uC624\uC870\uCD08\uCF54\uD1A0\uD3EC\uD638\uAD6C\uB204\uB450\uB8E8\uBB34\uBD80\uC218\uC6B0\uC8FC\uCD94\uCFE0\uD22C\uD478\uD6C4\uADF8\uB290\uB4DC\uB974\uBBC0\uBE0C\uC2A4\uC73C\uC988\uCE20\uD06C\uD2B8\uD504\uD750\uAE30\uB2C8\uB514\uB9AC\uBBF8\uBE44\uC2DC\uC774\uC9C0\uCE58\uD0A4\uD2F0\uD53C\uD788\uC0AC\uB791\uBCC4\uBE5B\uB9C8\uC74C\uB178\uB798\uD558\uB298\uBC14\uB78C\uAFC8\uB208\uBB3C\uB108\uB098\uC6B0\uB9AC";
  const EN_U = "ABCDEFGHIJKLMNOPQRSTUVWXYZ", EN_L = "abcdefghijklmnopqrstuvwxyz", DIG = "0123456789", SYM = "\uFF03\uFF0A\uFF0B\uFF1D\uFF0F\uFF1C\uFF1E\u203B\u25C7\u25C6\u25A1\u25B3\u25CB";
  J.POOLS = {
    ja: {
      kana: "\u30A2\u30A4\u30A6\u30A8\u30AA\u30AB\u30AD\u30AF\u30B1\u30B3\u30B5\u30B7\u30B9\u30BB\u30BD\u30BF\u30C1\u30C4\u30C6\u30C8\u30CA\u30CB\u30CC\u30CD\u30CE\u30CF\u30D2\u30D5\u30D8\u30DB\u30DE\u30DF\u30E0\u30E1\u30E2\u30E4\u30E6\u30E8\u30E9\u30EA\u30EB\u30EC\u30ED\u30EF\u30F2\u30F3",
      hira: "\u3042\u3044\u3046\u3048\u304A\u304B\u304D\u304F\u3051\u3053\u3055\u3057\u3059\u305B\u305D\u305F\u3061\u3064\u3066\u3068\u306A\u306B\u306C\u306D\u306E\u306F\u3072\u3075\u3078\u307B\u307E\u307F\u3080\u3081\u3082\u3084\u3086\u3088\u3089\u308A\u308B\u308C\u308D\u308F\u3092\u3093",
      half: "\uFF71\uFF72\uFF73\uFF74\uFF75\uFF76\uFF77\uFF78\uFF79\uFF7A\uFF7B\uFF7C\uFF7D\uFF7E\uFF7F\uFF80\uFF81\uFF82\uFF83\uFF84\uFF85\uFF86\uFF87\uFF88\uFF89\uFF8A\uFF8B\uFF8C\uFF8D\uFF8E\uFF8F\uFF90\uFF91\uFF92\uFF93\uFF94\uFF95\uFF96\uFF97\uFF98\uFF99\uFF9A\uFF9B\uFF9C\uFF9D0123456789",
      reel: "\u5922\u5149\u5F71\u7A7A\u591C\u661F\u96E8\u6D99\u5FC3\u604B\u58F0\u82B1\u6708\u98A8\u611B\u5618\u7F6A\u8272\u97F3\u6D77\u30A2\u30A4\u30A6\u30A8\u30AA\u30AB\u30AD\u30AF\u30B1\u30B3\u30B5\u30B7\u30B9\u30BB\u30BD0123456789",
      scramble: "\u30A2\u30A4\u30A6\u30A8\u30AA\u30AB\u30AD\u30AF\u30B1\u30B3\u30B5\u30B7\u30B9\u30BB\u30BD\u30BF\u30C1\u30C4\u30C6\u30C8\u30CA\u30CB\u30CC\u30CD\u30CE\u30CF\u30D2\u30D5\u30D8\u30DB\u30DE\u30DF\u30E0\u30E1\u30E2\u30E4\u30E6\u30E8\u30E9\u30EA\u30EB\u30EC\u30ED\u30EF\u30F2\u30F3\u611B\u54C0\u5922\u5618\u58F0\u5149\u5F71\u7A7A\u591C\u661F\u96E8\u6D99\u5FC3\u604B\u7F6A\u795E\u5618\u58CA\u53EB\u865A\u2605\u25C6\u25B2\u25CF\u25A0\u203B\uFF03\uFF04\uFF05\uFF0601234567ABCDEFGHJKLMNPQRSTUVWXYZ",
      signs: "\u30A2\u30A4\u30A6\u30A8\u30AA\u30AB\u30AD\u30AF\u30B1\u30B3\u30B5\u30B7\u30B9\u30BB\u30BD\u30BF\u30C1\u30C4\u30C6\u30C8\u30CA\u30CB\u30CC\u30CD\u30CE\u30CF\u30D2\u30D5\u30D8\u30DB\u30DE\u30DF\u30E0\u30E1\u30E2\u30E4\u30E6\u30E8\u30E9\u30EA\u30EB\u30EC\u30ED\u30EF\u30F2\u30F3\uFF03\uFF0A\uFF0B\uFF1D\uFF0F\uFF1C\uFF1E\u203B\u25C7\u25C6\u25A1\u25B3\u25CB01"
    },
    "zh-Hant": { kana: ZH_T, hira: ZH_T, half: ZH_T.slice(0, 60) + DIG, reel: ZH_T.slice(-40) + DIG, scramble: ZH_T + "\u2605\u25C6\u25B2\u25CF\u25A0\u203B\uFF03\uFF04\uFF05\uFF06" + DIG, signs: ZH_T.slice(0, 60) + SYM + "01" },
    "zh-Hans": { kana: ZH_S, hira: ZH_S, half: ZH_S.slice(0, 60) + DIG, reel: ZH_S.slice(-40) + DIG, scramble: ZH_S + "\u2605\u25C6\u25B2\u25CF\u25A0\u203B\uFF03\uFF04\uFF05\uFF06" + DIG, signs: ZH_S.slice(0, 60) + SYM + "01" },
    ko: { kana: KO, hira: KO, half: KO.slice(0, 60) + DIG, reel: KO.slice(-30) + DIG, scramble: KO + "\u2605\u25C6\u25B2\u25CF\u25A0\u203B\uFF03\uFF04\uFF05\uFF06" + DIG, signs: KO.slice(0, 60) + SYM + "01" },
    en: { kana: EN_U, hira: EN_L, half: "0123456789ABCDEF", reel: EN_U + DIG, scramble: EN_U + EN_L + "\u2605\u25C6\u25B2\u25CF\u25A0#$%&" + DIG, signs: EN_U + "#*+=/<>01" }
  };
  J.pool = (kind) => {
    const P = J.POOLS[J.lang] || J.POOLS.ja;
    return P[kind] || J.POOLS.ja[kind];
  };
  const SERIF_KINDS = { mincho: 1, brush: 1, hand: 1 };
  const faceCache = /* @__PURE__ */ new Map();
  J.faceOf = (key) => {
    const f = J.FONTS[key] || J.FONTS.gothic_bold;
    const L = J.LANG_FACES[J.lang];
    if (!L || f.user) return f;
    const ck = J.lang + "|" + key;
    let r = faceCache.get(ck);
    if (r) return r;
    const m = L.map[key], fb = (SERIF_KINDS[f.kind] ? L.fbSerif : L.fbSans) + "," + f.fb;
    r = !m ? { family: f.family, weight: f.weight, fb, gf: f.gf, label: f.label, name: f.label, kind: f.kind } : { family: '"' + m.family + '"', weight: m.weight, fb, gf: m.gf, label: m.family + (m.weight !== 400 ? " " + m.weight : ""), name: m.family, kind: f.kind };
    faceCache.set(ck, r);
    return r;
  };
  J.langBaseFaces = (keys) => {
    const L = J.LANG_FACES[J.lang];
    if (!L) return [];
    const out = [L.sans];
    if ((keys || []).some((k) => J.FONTS[k] && SERIF_KINDS[J.FONTS[k].kind])) out.push(L.serif);
    return out;
  };
  J.segLocale = () => J.lang === "zh-Hant" ? "zh-Hant" : J.lang === "zh-Hans" ? "zh-Hans" : J.lang === "ko" ? "ko" : J.lang === "en" ? "en" : "ja";
})();
