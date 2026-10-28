import { exampleDefinition } from '@apl-studio/model'
import { writeFileSync } from 'node:fs'
let n = 0
writeFileSync('examples/air-care.apl.json', JSON.stringify(exampleDefinition(() => 'field_' + (++n)), null, 2) + '\n')
