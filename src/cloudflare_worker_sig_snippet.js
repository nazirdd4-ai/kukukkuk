// cloudflare_worker_sig_snippet.js — paste into the existing worker. Not a full worker,
// just the piece that turns {valid} into {valid, sig}.
//
// Ed25519, not HMAC: the OLD version of this file signed with an HMAC key the CLIENT also had
// to carry to verify — one extraction of that key from any shipped build forged passes forever.
// Ed25519 is asymmetric: only this Worker holds SIG_PRIVKEY; the client carries ED25519_PUB,
// which is not a secret (see .github/workflows/build_obus.yml's header for how it's generated
// and how it reaches the client build). Losing/rotating the public side is a build-time change,
// not a security boundary; losing the private side means re-keying (generate a new pair, update
// both this secret and the next build's ED25519_PUB — old binaries keep trusting the old pubkey).
//
// Set SIG_PRIVKEY as a Worker secret:
//   wrangler secret put SIG_PRIVKEY
// with the 64-hex-char Ed25519 PRIVATE seed from the SAME keypair whose public half was put in
// this repo's ED25519_PUB GitHub Actions secret — see that workflow's header for generation.
//
// UNVERIFIED: Cloudflare Workers' WebCrypto exposes Ed25519 under the non-standard algorithm name
// "NODE-ED25519" (confirmed via Cloudflare/MDN docs at time of writing), importable from a JWK
// {kty:"OKP", crv:"Ed25519", d, x} built from the raw 32-byte seed/pubkey per RFC 8037 — this
// path has NOT been exercised against a live Worker here. Deploy to a staging route and check
// crypto.subtle.sign() doesn't throw before relying on it; if NODE-ED25519/JWK import behaves
// differently than documented, the fallback is to pre-sign nothing and verify by shelling out to
// a small sidecar that has a real libsodium — but try this path first, it needs no extra service.

function b64url(bytes) {
  let s = btoa(String.fromCharCode(...bytes));
  return s.replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '');
}

async function importEd25519PrivateKey(seedHex, pubHex) {
  const d = b64url(hexToBytes(seedHex));
  const x = b64url(hexToBytes(pubHex));
  return crypto.subtle.importKey(
    'jwk',
    { kty: 'OKP', crv: 'Ed25519', d, x, key_ops: ['sign'], ext: true },
    { name: 'NODE-ED25519', namedCurve: 'NODE-ED25519' },
    false,
    ['sign']
  );
}

// env.ED25519_PUB_HEX: the same public hex as the repo/client secret — needed here only because
// JWK private-key import wants the x (public) component alongside d (seed); not kept secret.
async function signLicenseResult(licenseKey, valid, env) {
  const cryptoKey = await importEd25519PrivateKey(env.SIG_PRIVKEY, env.ED25519_PUB_HEX);
  const msg = new TextEncoder().encode(`${licenseKey}|${valid ? '1' : '0'}`);
  const sigBuf = await crypto.subtle.sign({ name: 'NODE-ED25519' }, cryptoKey, msg);
  return bytesToHex(new Uint8Array(sigBuf));   // 64 bytes -> 128 hex chars
}

function hexToBytes(hex) {
  const out = new Uint8Array(hex.length / 2);
  for (let i = 0; i < out.length; i++) out[i] = parseInt(hex.substr(i * 2, 2), 16);
  return out;
}
function bytesToHex(bytes) {
  return Array.from(bytes).map(b => b.toString(16).padStart(2, '0')).join('');
}

// Wherever the worker currently does:
//   return new Response(JSON.stringify({ valid: true }), ...);
// change to:
//   const sig = await signLicenseResult(licenseKey, true, env);
//   return new Response(JSON.stringify({ valid: true, sig }), ...);
//
// licenseKey here must be the exact string the client sent in {"licenseKey":"<KEY>"} — the
// client signs/verifies against that literal value, not a normalized or re-cased copy.
// Rejections ({valid:false}) don't need a sig — the client only checks it when valid:true,
// since a forged *rejection* gains an attacker nothing.
