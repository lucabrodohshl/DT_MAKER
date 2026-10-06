/**
 * Syntax highlighting for the aligner's formats (.ont, .interp) and UPPAAL XML.
 * Highlighting is cosmetic: the authoritative parse is the backend's strict parser
 * plus the aligner's own (diagnostics arrive through the lint extension).
 */
import { HighlightStyle, StreamLanguage, syntaxHighlighting, type StreamParser } from '@codemirror/language';
import { tags as t } from '@lezer/highlight';

const SMT_OPERATORS = new Set([
  'and', 'or', 'not', '=>', 'ite', 'distinct', 'forall', 'exists', 'let', 'true', 'false', 'div', 'mod', 'abs', 'to_real', 'to_int',
]);
const DECL_KEYWORDS = new Set(['sort', 'fun', 'rel', 'axiom']);

interface State {
  lineStart: boolean;
  afterKeyword: boolean;
}

const ontologyParser: StreamParser<State> = {
  startState: () => ({ lineStart: true, afterKeyword: false }),
  token(stream, state) {
    if (stream.sol()) {
      state.lineStart = true;
      state.afterKeyword = false;
    }
    if (stream.eatSpace()) return null;
    if (stream.peek() === ';') {
      stream.skipToEnd();
      return 'comment';
    }
    if (stream.match(/^-?\d+(\.\d+)?/)) return 'number';
    if (stream.match(/^[()]/)) return 'paren';
    if (stream.match(/^(<=|>=|=>|[<>=+\-*/])/)) return 'operator';
    if (stream.match(':')) return 'punctuation';
    if (stream.match('->')) return 'operator';
    const word = stream.match(/^[A-Za-z_][\w.]*!?/) as RegExpMatchArray | null;
    if (word) {
      const w = word[0];
      if (state.lineStart && DECL_KEYWORDS.has(w)) {
        state.lineStart = false;
        state.afterKeyword = true;
        return 'keyword';
      }
      if (state.afterKeyword) {
        state.afterKeyword = false;
        return 'definition';
      }
      if (state.lineStart) {
        state.lineStart = false;
        return w.endsWith('!') ? 'labelName' : 'definition';
      }
      if (SMT_OPERATORS.has(w)) return 'operatorKeyword';
      if (/^[A-Z]/.test(w)) return 'typeName';
      return 'variableName';
    }
    stream.next();
    return null;
  },
  tokenTable: {
    definition: t.definition(t.variableName),
    labelName: t.labelName,
    operatorKeyword: t.operatorKeyword,
    typeName: t.typeName,
  },
};

export const ontologyLanguage = StreamLanguage.define(ontologyParser);

const xmlParser: StreamParser<{ inTag: boolean }> = {
  startState: () => ({ inTag: false }),
  token(stream, state) {
    if (stream.match('<!--')) {
      while (!stream.eol() && !stream.match('-->')) stream.next();
      return 'comment';
    }
    if (!state.inTag && stream.match(/^<\/?[\w:-]+/)) {
      state.inTag = true;
      return 'tagName';
    }
    if (state.inTag) {
      if (stream.match(/^\/?>/)) {
        state.inTag = false;
        return 'tagName';
      }
      if (stream.match(/^"[^"]*"/)) return 'string';
      if (stream.match(/^[\w:-]+/)) return 'attributeName';
    }
    if (stream.match(/^&\w+;/)) return 'operator';
    stream.next();
    return null;
  },
};

export const xmlLanguage = StreamLanguage.define(xmlParser);

export const studioHighlight = syntaxHighlighting(
  HighlightStyle.define([
    { tag: t.comment, color: 'var(--text-subtle)', fontStyle: 'italic' },
    { tag: t.keyword, color: 'var(--formal)', fontWeight: '600' },
    { tag: t.definition(t.variableName), color: 'var(--accent-text)', fontWeight: '600' },
    { tag: t.labelName, color: 'var(--series-2)', fontWeight: '600' },
    { tag: t.operatorKeyword, color: 'var(--formal)' },
    { tag: t.typeName, color: 'var(--series-3)' },
    { tag: t.number, color: 'var(--series-5)' },
    { tag: t.operator, color: 'var(--text-muted)' },
    { tag: t.tagName, color: 'var(--accent-text)' },
    { tag: t.attributeName, color: 'var(--series-2)' },
    { tag: t.string, color: 'var(--series-3)' },
  ]),
);
