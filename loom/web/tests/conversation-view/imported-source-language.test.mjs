// Catalog-only labels on a server-rendered source inspector: no browser, sockets, model or private exports.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import React from 'react';
import { renderToStaticMarkup } from 'react-dom/server';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
test.after(() => loader.close());
const { default: ImportedMessageContent } = await loader.load('src/components/ImportedMessageContent.tsx');
const { ShellText, shellTranslator } = await loader.load('src/shell/text.ts');
const { resolvePresentation, resolvePresentationFeature } = await loader.load('src/onboarding/presentation.mjs');
const fixture = JSON.parse(await readFile(new URL('../../e2e/fixtures/imported-message-content.json', import.meta.url), 'utf8'));
const message = fixture.messages.anthropic[0];
const feature = resolvePresentationFeature(resolvePresentation(), 'ui_shell');

test('source inspector uses the catalog of every offered language without changing the retained message', () => {
  const before = JSON.stringify(message);
  for (const [language, catalog] of Object.entries(feature.locales)) {
    const html = renderToStaticMarkup(React.createElement(ShellText.Provider, { value: shellTranslator(feature, language) },
      React.createElement(ImportedMessageContent, { message, includeVisibleText: false,
        initialView: { blocks: false, artifacts: false, references: false, wrapCode: false } })));
    assert.ok(html.includes(catalog.src_options), `${language}: source controls use their catalog`);
    assert.ok(html.includes(catalog.src_wrap), `${language}: wrapping uses its catalog`);
    assert.ok(!html.includes('src_summary'), `${language}: summary is translated`);
    assert.ok(!html.includes('{{provider}}'), `${language}: source parameters are expanded`);
    assert.equal(JSON.stringify(message), before, `${language}: retained source is unchanged`);
  }
});
