# SPDX-License-Identifier: Apache-2.0
"""Private packaged projection of the Interchange surface-publication/1 schema.

Owner: Meshvale/meshvale-interchange docs/surface-publication.schema.json.
Schema ID: urn:meshvale:surface-publication:1. No independent schema changes.
"""
import json

SOURCE_SHA256 = '8d2af90b71460b5b89c2c077337e11a360a63b60fefacc7c776f907f4be8c1e7'
SCHEMA = json.loads(r'''
{
  "type": "object",
  "properties": {
    "schema": {
      "const": "meshvale.surface-publication/1"
    },
    "phase": {
      "const": "staged-payload-verified"
    },
    "encoding": {
      "enum": [
        "gltf-json",
        "glb",
        "stl-ascii",
        "stl-binary"
      ]
    },
    "representation": {
      "enum": [
        "strict-bare",
        "annotated-bundle-v1"
      ]
    },
    "entry_path": {
      "$ref": "#/$defs/payload_path"
    },
    "payload_members": {
      "type": "array",
      "minItems": 1,
      "uniqueItems": true,
      "items": {
        "$ref": "#/$defs/payload_member"
      },
      "description": "UTF-8-path sorted exact unique closed payload inventory, including surface.json if present and excluding the receipt itself. Native path uniqueness, complete inventory, byte/hash and proof checks are mandatory."
    },
    "observations": {
      "type": "object",
      "properties": {
        "payload_write": {
          "type": "object",
          "properties": {
            "status": {
              "const": "complete"
            },
            "bytes_written": {
              "$ref": "#/$defs/u64"
            },
            "closed_members": {
              "$ref": "#/$defs/u64"
            }
          },
          "required": [
            "status",
            "bytes_written",
            "closed_members"
          ],
          "additionalProperties": false
        },
        "payload_reread": {
          "type": "object",
          "properties": {
            "status": {
              "const": "complete"
            },
            "bytes_read": {
              "$ref": "#/$defs/u64"
            },
            "members": {
              "$ref": "#/$defs/u64"
            }
          },
          "required": [
            "status",
            "bytes_read",
            "members"
          ],
          "additionalProperties": false
        },
        "fresh_inspection": {
          "type": "object",
          "properties": {
            "status": {
              "const": "success"
            },
            "members": {
              "$ref": "#/$defs/u64"
            }
          },
          "required": [
            "status",
            "members"
          ],
          "additionalProperties": false
        }
      },
      "required": [
        "payload_write",
        "payload_reread",
        "fresh_inspection"
      ],
      "additionalProperties": false
    },
    "caller_report": {
      "anyOf": [
        {
          "type": "null"
        },
        {
          "$ref": "#/$defs/caller_report"
        }
      ]
    }
  },
  "required": [
    "schema",
    "phase",
    "encoding",
    "representation",
    "entry_path",
    "payload_members",
    "observations",
    "caller_report"
  ],
  "additionalProperties": false,
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "$id": "urn:meshvale:surface-publication:1",
  "title": "Meshvale surface publication receipt version 1",
  "description": "Development native module. Structural shape only: surface-files.md additionally requires exact native inventory, lengths/hashes/u64 ranges, cumulative counts, fresh physical inspection, UTF-8 report bytes and temporal truth. A schema pass cannot create a proof or certify commit. No own receipt hash or postcommit facts are represented.",
  "$defs": {
    "u64": {
      "type": "string",
      "pattern": "^(?:0|[1-9][0-9]{0,19})(?![\\s\\S])",
      "description": "Canonical decimal uint64; native semantic check additionally requires <=18446744073709551615 and the selected host/cap extent."
    },
    "sha256": {
      "type": "string",
      "pattern": "^[0-9a-f]{64}(?![\\s\\S])"
    },
    "payload_path": {
      "type": "string",
      "pattern": "^(?:surface\\.(?:gltf|glb|stl|json|bin)|geometry\\.bin|images/image-(?:0|[1-9][0-9]*)\\.(?:png|jpg))(?![\\s\\S])"
    },
    "payload_member": {
      "type": "object",
      "properties": {
        "kind": {
          "enum": [
            "gltf",
            "glb",
            "stl",
            "geometry-buffer",
            "image",
            "capsule-json",
            "capsule-binary"
          ]
        },
        "path": {
          "$ref": "#/$defs/payload_path"
        },
        "byte_length": {
          "$ref": "#/$defs/u64"
        },
        "sha256": {
          "$ref": "#/$defs/sha256"
        }
      },
      "required": [
        "kind",
        "path",
        "byte_length",
        "sha256"
      ],
      "additionalProperties": false,
      "allOf": [
        {
          "if": {
            "properties": {
              "kind": {
                "const": "gltf"
              }
            },
            "required": [
              "kind"
            ]
          },
          "then": {
            "properties": {
              "path": {
                "const": "surface.gltf"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "kind": {
                "const": "glb"
              }
            },
            "required": [
              "kind"
            ]
          },
          "then": {
            "properties": {
              "path": {
                "const": "surface.glb"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "kind": {
                "const": "stl"
              }
            },
            "required": [
              "kind"
            ]
          },
          "then": {
            "properties": {
              "path": {
                "const": "surface.stl"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "kind": {
                "const": "geometry-buffer"
              }
            },
            "required": [
              "kind"
            ]
          },
          "then": {
            "properties": {
              "path": {
                "const": "geometry.bin"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "kind": {
                "const": "capsule-json"
              }
            },
            "required": [
              "kind"
            ]
          },
          "then": {
            "properties": {
              "path": {
                "const": "surface.json"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "kind": {
                "const": "capsule-binary"
              }
            },
            "required": [
              "kind"
            ]
          },
          "then": {
            "properties": {
              "path": {
                "const": "surface.bin"
              }
            }
          }
        },
        {
          "if": {
            "properties": {
              "kind": {
                "const": "image"
              }
            },
            "required": [
              "kind"
            ]
          },
          "then": {
            "properties": {
              "path": {
                "type": "string",
                "pattern": "^images/image-(?:0|[1-9][0-9]*)\\.(?:png|jpg)(?![\\s\\S])"
              }
            }
          }
        }
      ]
    },
    "caller_report": {
      "type": "object",
      "properties": {
        "encoding": {
          "const": "utf-8"
        },
        "byte_length": {
          "$ref": "#/$defs/u64"
        },
        "sha256": {
          "$ref": "#/$defs/sha256"
        },
        "text": {
          "type": "string",
          "description": "Uninterpreted exact caller UTF-8 text, including U+0000; native length/digest apply to the original unescaped UTF-8 bytes. No report semantics or standalone filename certification."
        }
      },
      "required": [
        "encoding",
        "byte_length",
        "sha256",
        "text"
      ],
      "additionalProperties": false
    }
  },
  "allOf": [
    {
      "if": {
        "properties": {
          "encoding": {
            "const": "gltf-json"
          }
        },
        "required": [
          "encoding"
        ]
      },
      "then": {
        "properties": {
          "entry_path": {
            "const": "surface.gltf"
          },
          "payload_members": {
            "contains": {
              "properties": {
                "path": {
                  "const": "surface.gltf"
                }
              },
              "required": [
                "path"
              ]
            },
            "minContains": 1,
            "maxContains": 1
          }
        }
      }
    },
    {
      "if": {
        "properties": {
          "encoding": {
            "const": "glb"
          }
        },
        "required": [
          "encoding"
        ]
      },
      "then": {
        "properties": {
          "entry_path": {
            "const": "surface.glb"
          },
          "payload_members": {
            "contains": {
              "properties": {
                "path": {
                  "const": "surface.glb"
                }
              },
              "required": [
                "path"
              ]
            },
            "minContains": 1,
            "maxContains": 1
          }
        }
      }
    },
    {
      "if": {
        "properties": {
          "encoding": {
            "const": "stl-ascii"
          }
        },
        "required": [
          "encoding"
        ]
      },
      "then": {
        "properties": {
          "entry_path": {
            "const": "surface.stl"
          },
          "payload_members": {
            "contains": {
              "properties": {
                "path": {
                  "const": "surface.stl"
                }
              },
              "required": [
                "path"
              ]
            },
            "minContains": 1,
            "maxContains": 1
          }
        }
      }
    },
    {
      "if": {
        "properties": {
          "encoding": {
            "const": "stl-binary"
          }
        },
        "required": [
          "encoding"
        ]
      },
      "then": {
        "properties": {
          "entry_path": {
            "const": "surface.stl"
          },
          "payload_members": {
            "contains": {
              "properties": {
                "path": {
                  "const": "surface.stl"
                }
              },
              "required": [
                "path"
              ]
            },
            "minContains": 1,
            "maxContains": 1
          }
        }
      }
    },
    {
      "if": {
        "properties": {
          "representation": {
            "const": "annotated-bundle-v1"
          }
        },
        "required": [
          "representation"
        ]
      },
      "then": {
        "properties": {
          "payload_members": {
            "allOf": [
              {
                "contains": {
                  "properties": {
                    "kind": {
                      "const": "capsule-json"
                    }
                  },
                  "required": [
                    "kind"
                  ]
                },
                "minContains": 1,
                "maxContains": 1
              },
              {
                "contains": {
                  "properties": {
                    "kind": {
                      "const": "capsule-binary"
                    }
                  },
                  "required": [
                    "kind"
                  ]
                },
                "minContains": 1,
                "maxContains": 1
              }
            ]
          }
        }
      },
      "else": {
        "properties": {
          "payload_members": {
            "items": {
              "not": {
                "properties": {
                  "kind": {
                    "enum": [
                      "capsule-json",
                      "capsule-binary"
                    ]
                  }
                },
                "required": [
                  "kind"
                ]
              }
            }
          }
        }
      }
    }
  ]
}
''')
