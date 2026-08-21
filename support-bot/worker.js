/**
 * OMEGAWARE GTi — support bot Worker
 *
 * Holds the API key, holds the knowledge base, talks to the Claude API,
 * streams the answer back to docs/help.html.
 *
 * Deploy:
 *   wrangler kv namespace create GTI_KV      # paste id into wrangler.toml
 *   wrangler secret put ANTHROPIC_API_KEY
 *   wrangler deploy
 */

import KB from "../SUPPORT.md";

const MODEL = "claude-haiku-4-5-20251001";
const MAX_TOKENS = 700;          // support answers are short; output is 5x the price of input
const MAX_TURNS = 24;            // messages kept per conversation
const MAX_CHARS = 2000;          // per user message

const RULES = `You are the support assistant for OMEGAWARE GTi, a touchscreen interface for
Gotek floppy emulators used in Amiga and other retro computers. You are answering the owner
of a GTi who has hit a problem.

HOW TO ANSWER
- Answer ONLY from the DOCUMENTATION below. It is the single source of truth.
- If the documentation does not cover it, say so plainly and send them to the OMEGAWARE
  Discord (https://discord.gg/7gY4PKUnnf) for a quick answer, or GitHub issues
  (https://github.com/mesarim/Gotek-Touchscreen-interface/issues) for a bug. Do not guess.
- NEVER invent CONFIG.TXT keys, version numbers, file names, menu labels or pin names.
  If you cannot find the exact key or version in the documentation, do not state one.
- Firmware moves fast. Avoid quoting version numbers; point people at the flasher page
  (https://mesarim.github.io/Gotek-Touchscreen-interface/) which is always current.
- The settings button is labelled INFO on most builds and CONFIG on newer ones. When giving
  steps, mention both the first time so the user can find it whichever they have.
- Be brief. Two or three short paragraphs, or a numbered list of steps. These are people
  standing over an open Amiga, not readers.
- Plain, friendly, technical. Retro-computing literate. No corporate padding, no
  "I'd be happy to help", no apologising for things that are not your fault.
- Ask a clarifying question when the answer genuinely depends on which board or mode they
  have — but only one, and only when it changes the answer.
- If someone seems to be describing a bug rather than a setup problem, help as far as the
  documentation goes, then encourage them to open a GitHub issue with their firmware
  version, board and SD card.
- Refuse politely and briefly if asked to do anything unrelated to GTi support. You are not
  a general-purpose assistant.
- Never discuss pricing, margins, other customers, or unreleased plans. If asked about
  buying, point them at the GitHub page.

DOCUMENTATION
=============
`;

export default {
  async fetch(request, env, ctx) {
    const origin = request.headers.get("Origin") || "";
    const allowed = (env.ALLOWED_ORIGINS || "").split(",").map((s) => s.trim());
    const cors = {
      "Access-Control-Allow-Origin": allowed.includes(origin) ? origin : allowed[0] || "*",
      "Access-Control-Allow-Methods": "POST, OPTIONS",
      "Access-Control-Allow-Headers": "Content-Type",
      "Access-Control-Max-Age": "86400",
      Vary: "Origin",
    };

    if (request.method === "OPTIONS") return new Response(null, { status: 204, headers: cors });
    if (request.method !== "POST") return err(405, "Use POST.", cors);

    // ---- rate limit -------------------------------------------------------
    const ip = request.headers.get("CF-Connecting-IP") || "unknown";
    const limit = parseInt(env.RATE_LIMIT || "40", 10);
    const bucket = `rl:${ip}:${Math.floor(Date.now() / 3600000)}`;
    const used = parseInt((await env.GTI_KV.get(bucket)) || "0", 10);
    if (used >= limit) {
      return err(429, "That's a lot of questions in one hour. Try again shortly, or open a GitHub issue.", cors);
    }
    ctx.waitUntil(env.GTI_KV.put(bucket, String(used + 1), { expirationTtl: 3700 }));

    // ---- validate input ---------------------------------------------------
    let body;
    try {
      body = await request.json();
    } catch {
      return err(400, "Bad request body.", cors);
    }

    const messages = Array.isArray(body.messages) ? body.messages.slice(-MAX_TURNS) : null;
    if (!messages || !messages.length) return err(400, "No messages.", cors);

    for (const m of messages) {
      if (m.role !== "user" && m.role !== "assistant") return err(400, "Bad role.", cors);
      if (typeof m.content !== "string" || !m.content.length) return err(400, "Bad content.", cors);
      if (m.content.length > MAX_CHARS) return err(400, "Message too long — trim it down a bit.", cors);
    }
    if (messages[messages.length - 1].role !== "user") return err(400, "Last message must be from the user.", cors);

    // ---- call the API -----------------------------------------------------
    const upstream = await fetch("https://api.anthropic.com/v1/messages", {
      method: "POST",
      headers: {
        "content-type": "application/json",
        "x-api-key": env.ANTHROPIC_API_KEY,
        "anthropic-version": "2023-06-01",
      },
      body: JSON.stringify({
        model: MODEL,
        max_tokens: MAX_TOKENS,
        stream: true,
        system: [
          {
            type: "text",
            text: RULES + KB,
            // The whole rules+KB block is static, so it caches. Cache reads bill at 10%
            // of input. Keep this block FIRST and never interpolate anything into it,
            // or every token silently goes back to full price.
            cache_control: { type: "ephemeral" },
          },
        ],
        messages,
      }),
    });

    if (!upstream.ok || !upstream.body) {
      const detail = await upstream.text().catch(() => "");
      console.log("upstream error", upstream.status, detail.slice(0, 500));
      return err(502, "The support assistant is unreachable right now. Please open a GitHub issue.", cors);
    }

    // ---- stream through, accumulating for the log -------------------------
    const encoder = new TextEncoder();
    const decoder = new TextDecoder();
    let answer = "";
    let carry = "";

    const out = new ReadableStream({
      async start(controller) {
        const reader = upstream.body.getReader();
        try {
          for (;;) {
            const { done, value } = await reader.read();
            if (done) break;
            carry += decoder.decode(value, { stream: true });
            const lines = carry.split("\n");
            carry = lines.pop() || "";
            for (const line of lines) {
              if (!line.startsWith("data:")) continue;
              let evt;
              try {
                evt = JSON.parse(line.slice(5).trim());
              } catch {
                continue;
              }
              if (evt.type === "content_block_delta" && evt.delta?.type === "text_delta") {
                answer += evt.delta.text;
                controller.enqueue(encoder.encode(`data: ${JSON.stringify({ text: evt.delta.text })}\n\n`));
              }
            }
          }
          controller.enqueue(encoder.encode(`data: ${JSON.stringify({ done: true })}\n\n`));
        } catch (e) {
          console.log("stream error", String(e));
          controller.enqueue(encoder.encode(`data: ${JSON.stringify({ done: true })}\n\n`));
        } finally {
          controller.close();
          ctx.waitUntil(logIt(env, messages, answer));
        }
      },
    });

    return new Response(out, {
      headers: {
        ...cors,
        "content-type": "text/event-stream; charset=utf-8",
        "cache-control": "no-cache",
      },
    });
  },
};

/**
 * Every conversation lands in KV. This log is the point of the whole exercise:
 * it tells you what people actually ask, which tells you what to fix in the
 * firmware instead of documenting round.
 *   wrangler kv key list --binding GTI_KV --prefix log:
 */
async function logIt(env, messages, answer) {
  try {
    const days = parseInt(env.LOG_RETENTION_DAYS || "90", 10);
    const key = `log:${new Date().toISOString()}:${crypto.randomUUID().slice(0, 8)}`;
    await env.GTI_KV.put(
      key,
      JSON.stringify({
        at: new Date().toISOString(),
        asked: messages.filter((m) => m.role === "user").map((m) => m.content),
        answered: answer,
      }),
      { expirationTtl: days * 86400 }
    );
  } catch (e) {
    console.log("log failed", String(e));
  }
}

function err(status, message, cors) {
  return new Response(JSON.stringify({ error: message }), {
    status,
    headers: { ...cors, "content-type": "application/json" },
  });
}
