#!/usr/bin/env node
/**
 * Converts the exported (non-generic) interfaces and type aliases of a TypeScript file into
 * JSON Schema (2020-12, as used by OpenAPI 3.1) using the TypeScript type checker.
 *
 *   node scripts/ts-to-jsonschema.mjs src/api/types.ts > schemas.json
 *
 * Output: {"<TypeName>": <schema>, ...}. References to other exported types become
 * {"$ref": "#/components/schemas/<TypeName>"}; intersections and `extends` are flattened.
 * Used by scripts/openapi/ to keep api/studio.openapi.yaml in step with the client types and
 * to cross-check src/runtime/types.ts against api/runtime.openapi.yaml.
 */
import ts from 'typescript';
import path from 'node:path';

const file = path.resolve(process.argv[2] ?? 'src/api/types.ts');
const program = ts.createProgram([file], { strict: true, target: ts.ScriptTarget.ES2022, noEmit: true, skipLibCheck: true });
const checker = program.getTypeChecker();
const source = program.getSourceFile(file);
if (!source) throw new Error(`cannot read ${file}`);

const exported = new Map(); // name -> declaration
for (const st of source.statements) {
  const isExport = st.modifiers?.some((m) => m.kind === ts.SyntaxKind.ExportKeyword);
  if (!isExport) continue;
  if ((ts.isInterfaceDeclaration(st) || ts.isTypeAliasDeclaration(st)) && !st.typeParameters?.length) exported.set(st.name.text, st);
}

const ref = (name) => ({ $ref: `#/components/schemas/${name}` });

/** Name under which `type` is exported from this file, if it is one of the named types. */
function namedRef(type) {
  const alias = type.aliasSymbol?.getName();
  if (alias && exported.has(alias) && !type.aliasTypeArguments?.length) return alias;
  const sym = type.getSymbol();
  if (sym && exported.has(sym.getName()) && (type.flags & ts.TypeFlags.Object) && sym.declarations?.some((d) => d.getSourceFile() === source)) {
    return sym.getName();
  }
  return null;
}

function literalValue(t) {
  if (t.isStringLiteral()) return t.value;
  if (t.isNumberLiteral()) return t.value;
  if (t.flags & ts.TypeFlags.BooleanLiteral) return checker.typeToString(t) === 'true';
  return undefined;
}

function convert(type, root) {
  if (!root) {
    const name = namedRef(type);
    if (name) return ref(name);
  }
  const f = type.flags;
  if (f & (ts.TypeFlags.Any | ts.TypeFlags.Unknown)) return {};
  if (f & ts.TypeFlags.String) return { type: 'string' };
  if (f & ts.TypeFlags.Number) return { type: 'number' };
  if (f & ts.TypeFlags.Boolean) return { type: 'boolean' };
  if (f & ts.TypeFlags.Null) return { type: 'null' };
  if (f & (ts.TypeFlags.StringLiteral | ts.TypeFlags.NumberLiteral | ts.TypeFlags.BooleanLiteral)) return { const: literalValue(type) };

  if (type.isUnion()) {
    let members = type.types.filter((t) => !(t.flags & ts.TypeFlags.Undefined));
    // TypeScript represents `boolean` as `true | false`.
    const bools = members.filter((t) => t.flags & ts.TypeFlags.BooleanLiteral);
    if (bools.length === 2) members = [...members.filter((t) => !(t.flags & ts.TypeFlags.BooleanLiteral)), { __boolean: true }];
    const nullable = members.some((t) => t.flags && t.flags & ts.TypeFlags.Null);
    const rest = members.filter((t) => !(t.flags && t.flags & ts.TypeFlags.Null));
    const lits = rest.filter((t) => t.flags && t.flags & (ts.TypeFlags.StringLiteral | ts.TypeFlags.NumberLiteral));
    if (lits.length === rest.length && lits.length > 0) {
      const values = lits.map(literalValue);
      return nullable ? { enum: [...values, null] } : { enum: values };
    }
    const parts = rest.map((t) => (t.__boolean ? { type: 'boolean' } : convert(t, false)));
    const simple = parts.every((p) => typeof p.type === 'string' && Object.keys(p).length === 1);
    if (simple) {
      const types = [...new Set(parts.map((p) => p.type))];
      if (nullable) types.push('null');
      return { type: types.length === 1 ? types[0] : types };
    }
    if (nullable) parts.push({ type: 'null' });
    return parts.length === 1 ? parts[0] : { anyOf: parts };
  }

  if (checker.isArrayType(type)) {
    const [item] = checker.getTypeArguments(type);
    return { type: 'array', items: convert(item, false) };
  }

  if (f & ts.TypeFlags.Object || type.isIntersection()) {
    const properties = {};
    const required = [];
    for (const prop of checker.getPropertiesOfType(type)) {
      const propType = checker.getTypeOfSymbol(prop);
      const optional = (prop.flags & ts.SymbolFlags.Optional) !== 0;
      const schema = convert(propType, false); // `undefined` of optional members is dropped by the union case
      const doc = ts.displayPartsToString(prop.getDocumentationComment(checker)).trim();
      properties[prop.getName()] = doc && !schema.$ref ? { ...schema, description: doc } : schema;
      if (!optional) required.push(prop.getName());
    }
    const out = { type: 'object', properties };
    if (required.length) out.required = required;
    const index = checker.getIndexInfoOfType(type, ts.IndexKind.String);
    if (index) out.additionalProperties = convert(index.type, false);
    if (!Object.keys(properties).length && !index) return { type: 'object' };
    return out;
  }
  return {};
}

const out = {};
for (const [name, decl] of exported) {
  const type = checker.getTypeAtLocation(decl.name);
  const schema = convert(type, true);
  const doc = ts.displayPartsToString(checker.getSymbolAtLocation(decl.name)?.getDocumentationComment(checker) ?? []).trim();
  out[name] = doc ? { description: doc, ...schema } : schema;
}
process.stdout.write(JSON.stringify(out, null, 2) + '\n');
