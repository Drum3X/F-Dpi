#!/usr/bin/env node
// F-Dpi: split a Qt .ts file's unfinished messages into JSON chunks for translation, and merge them back.
//
//   node script/ts_tool.js export res/translations/tr_TR.ts <outdir> [chunks=6]
//       writes <outdir>/chunk_N.json: [{ id, context, source, comment?, numerus? }]  (entities decoded)
//   node script/ts_tool.js import res/translations/tr_TR.ts <dir>
//       reads <dir>/*.out.json: { "<id>": "text" | ["form", ...] } and fills those messages in place
//   node script/ts_tool.js stats res/translations/tr_TR.ts
//   node script/ts_tool.js english-logs res/translations/tr_TR.ts
//       keeps the log panel English: a text that src/ and include/ pass to tr() only inside MW_show_log(...) calls gets its
//       English source as the translation. A text also used outside a log call (a dialog, a label) keeps its translation.
//
// Message ids are positions in file order, so export and import must run against the same .ts revision.
"use strict";
const fs = require("fs");
const path = require("path");

const MESSAGE_RE = /<message( numerus="yes")?>([\s\S]*?)<\/message>/g;
const CONTEXT_RE = /<context>\s*<name>([\s\S]*?)<\/name>([\s\S]*?)<\/context>/g;

const decode = (s) =>
    s.replace(/&lt;/g, "<").replace(/&gt;/g, ">").replace(/&quot;/g, '"').replace(/&apos;/g, "'").replace(/&amp;/g, "&");
const encode = (s) => s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
const tag = (body, name) => {
    const m = body.match(new RegExp(`<${name}>([\\s\\S]*?)</${name}>`));
    return m ? decode(m[1]) : undefined;
};

function messages(ts) {
    const out = [];
    let id = 0;
    for (const ctx of ts.matchAll(CONTEXT_RE)) {
        for (const msg of ctx[2].matchAll(MESSAGE_RE)) {
            const body = msg[2];
            const tr = body.match(/<translation([^>]*)>([\s\S]*?)<\/translation>|<translation([^>]*)\/>/);
            const attrs = tr ? tr[1] || tr[3] || "" : "";
            out.push({
                id: id++,
                context: decode(ctx[1]),
                source: tag(body, "source"),
                comment: tag(body, "comment") || tag(body, "extracomment"),
                numerus: msg[1] ? Math.max(1, (body.match(/<numerusform/g) || []).length) : undefined,
                unfinished: !tr || /type="unfinished"/.test(attrs),
                obsolete: /type="(obsolete|vanished)"/.test(attrs),
            });
        }
    }
    return out;
}

function exportChunks(tsPath, outDir, chunks) {
    const todo = messages(fs.readFileSync(tsPath, "utf8")).filter((m) => m.unfinished && !m.obsolete);
    fs.mkdirSync(outDir, { recursive: true });
    const size = Math.ceil(todo.length / chunks);
    for (let i = 0; i < chunks; i++) {
        const part = todo.slice(i * size, (i + 1) * size).map(({ unfinished, obsolete, ...m }) => m);
        if (part.length) fs.writeFileSync(path.join(outDir, `chunk_${i + 1}.json`), JSON.stringify(part, null, 1));
    }
    console.log(`${todo.length} unfinished messages -> ${chunks} chunk(s) in ${outDir}`);
}

function importChunks(tsPath, dir) {
    const translations = {};
    for (const f of fs.readdirSync(dir).filter((f) => f.endsWith(".out.json"))) {
        Object.assign(translations, JSON.parse(fs.readFileSync(path.join(dir, f), "utf8")));
    }
    let ts = fs.readFileSync(tsPath, "utf8");
    let id = 0, filled = 0;
    ts = ts.replace(MESSAGE_RE, (whole, numerus, body) => {
        const t = translations[id++];
        if (t === undefined || !/type="unfinished"/.test(body)) return whole;
        const forms = Array.isArray(t) ? t : [t];
        if (forms.some((f) => typeof f !== "string" || !f.length)) return whole;
        const translation = numerus
            ? `<translation>\n${forms.map((f) => `            <numerusform>${encode(f)}</numerusform>`).join("\n")}\n        </translation>`
            : `<translation>${encode(forms[0])}</translation>`;
        filled++;
        return whole.replace(/<translation[^>]*>[\s\S]*?<\/translation>|<translation[^>]*\/>/, translation);
    });
    fs.writeFileSync(tsPath, ts);
    console.log(`filled ${filled} of ${Object.keys(translations).length} provided translations`);
}

// C++ string literals: [{ start, end, value }], adjacent literals ("a" "b") joined.
function cppLiterals(text) {
    const out = [];
    const re = /"((?:[^"\\\n]|\\.)*)"/g;
    const unescape = { n: "\n", t: "\t", '"': '"', "'": "'", "\\": "\\" };
    let m;
    while ((m = re.exec(text))) {
        const value = m[1].replace(/\\(n|t|"|'|\\)/g, (_, c) => unescape[c]);
        const prev = out[out.length - 1];
        if (prev && /^\s*$/.test(text.slice(prev.end, m.index))) {
            prev.value += value;
            prev.end = re.lastIndex;
        } else {
            out.push({ start: m.index, end: re.lastIndex, value });
        }
    }
    return out;
}

// [start, end) of the arguments of every MW_show_log( ... ) call.
function logSpans(text) {
    const spans = [];
    const re = /\bMW_show_log\s*\(/g;
    let m;
    while ((m = re.exec(text))) {
        let depth = 1, i = re.lastIndex;
        for (; i < text.length && depth > 0; i++) {
            const c = text[i];
            if (c === '"' || c === "'") {
                for (i++; i < text.length && text[i] !== c; i++) if (text[i] === "\\") i++;
            } else if (c === "(") depth++;
            else if (c === ")") depth--;
        }
        spans.push([re.lastIndex, i]);
    }
    return spans;
}

function englishLogs(tsPath) {
    const files = [];
    const walk = (dir) => {
        for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
            const f = path.join(dir, e.name);
            if (e.isDirectory()) walk(f);
            else if (/\.(cpp|h|hpp|ui)$/.test(e.name)) files.push(f);
        }
    };
    const root = path.resolve(__dirname, "..");
    for (const d of ["src", "include"]) walk(path.join(root, d));
    // A literal tr() takes: right after "tr(", or after translate("Context", .
    const trText = /\b(?:tr|QT_TR_NOOP)\s*\(\s*$|\b(?:translate|QT_TRANSLATE_NOOP)\s*\(\s*"(?:[^"\\]|\\.)*"\s*,\s*$/;
    const inLog = new Set(), elsewhere = new Set();
    for (const f of files) {
        const text = fs.readFileSync(f, "utf8");
        if (f.endsWith(".ui")) {
            for (const m of text.matchAll(/<string[^>]*>([\s\S]*?)<\/string>/g)) elsewhere.add(decode(m[1]));
            continue;
        }
        const spans = logSpans(text);
        for (const lit of cppLiterals(text)) {
            if (!trText.test(text.slice(Math.max(0, lit.start - 200), lit.start))) continue;
            (spans.some(([a, b]) => lit.start >= a && lit.end <= b) ? inLog : elsewhere).add(lit.value);
        }
    }
    let ts = fs.readFileSync(tsPath, "utf8");
    let set = 0, shared = 0;
    ts = ts.replace(MESSAGE_RE, (whole, numerus, body) => {
        const source = tag(body, "source");
        if (!inLog.has(source) || /type="(obsolete|vanished)"/.test(body)) return whole;
        if (elsewhere.has(source)) {
            shared++;
            console.log(`  kept: ${source}`);
            return whole;
        }
        set++;
        // The app's name, as the other translations have it; ThroneCore, throne:// and thronectl stay.
        const english = encode(source.replace(/\bThrone\b/g, "F-Dpi"));
        const forms = Math.max(1, (body.match(/<numerusform/g) || []).length);
        const translation = numerus
            ? `<translation>\n${Array(forms).fill(`            <numerusform>${english}</numerusform>`).join("\n")}\n        </translation>`
            : `<translation>${english}</translation>`;
        return whole.replace(/<translation[^>]*>[\s\S]*?<\/translation>|<translation[^>]*\/>/, translation);
    });
    fs.writeFileSync(tsPath, ts);
    console.log(`${inLog.size} log texts: ${set} message(s) set to English, ${shared} kept (also used outside the log)`);
}

function stats(tsPath) {
    const all = messages(fs.readFileSync(tsPath, "utf8")).filter((m) => !m.obsolete);
    console.log(`${all.length} messages, ${all.filter((m) => m.unfinished).length} unfinished`);
}

const [cmd, tsPath, arg, chunks] = process.argv.slice(2);
if (cmd === "export") exportChunks(tsPath, arg, Number(chunks) || 6);
else if (cmd === "import") importChunks(tsPath, arg);
else if (cmd === "stats") stats(tsPath);
else if (cmd === "english-logs") englishLogs(tsPath);
else {
    console.error("usage: ts_tool.js export|import|stats|english-logs <file.ts> [dir] [chunks]");
    process.exit(2);
}
