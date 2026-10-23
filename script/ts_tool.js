#!/usr/bin/env node
// F-Dpi: split a Qt .ts file's unfinished messages into JSON chunks for translation, and merge them back.
//
//   node script/ts_tool.js export res/translations/tr_TR.ts <outdir> [chunks=6]
//       writes <outdir>/chunk_N.json: [{ id, context, source, comment?, numerus? }]  (entities decoded)
//   node script/ts_tool.js import res/translations/tr_TR.ts <dir>
//       reads <dir>/*.out.json: { "<id>": "text" | ["form", ...] } and fills those messages in place
//   node script/ts_tool.js stats res/translations/tr_TR.ts
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

function stats(tsPath) {
    const all = messages(fs.readFileSync(tsPath, "utf8")).filter((m) => !m.obsolete);
    console.log(`${all.length} messages, ${all.filter((m) => m.unfinished).length} unfinished`);
}

const [cmd, tsPath, arg, chunks] = process.argv.slice(2);
if (cmd === "export") exportChunks(tsPath, arg, Number(chunks) || 6);
else if (cmd === "import") importChunks(tsPath, arg);
else if (cmd === "stats") stats(tsPath);
else {
    console.error("usage: ts_tool.js export|import|stats <file.ts> [dir] [chunks]");
    process.exit(2);
}
