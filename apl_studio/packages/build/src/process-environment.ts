import { dirname, delimiter } from 'node:path'

/** Compilers and native product processes do not need deployment credentials. */
export function childEnvironment(compiler: string, parent: NodeJS.ProcessEnv = process.env): NodeJS.ProcessEnv {
  const environment: NodeJS.ProcessEnv = { ...parent, PATH: dirname(compiler) + delimiter + (parent.PATH ?? '') }
  for (const key of Object.keys(environment)) if (['APL_STUDIO_USERNAME', 'APL_STUDIO_PASSWORD'].includes(key.toUpperCase())) delete environment[key]
  return environment
}
