#include <trance/visual/builtin_patterns.h>

// Native V4 AST sources. V3 definitions remain compatibility fixtures.
namespace builtin {
std::string pattern_source_v4(uint32_t visual_type)
{
  switch (visual_type) {
  case 1: return R"v4({
  "version": 4,
  "root": {
    "kind": "parallel",
    "children": [
      {
        "node": {
          "kind": "parallel",
          "children": [
            {
              "node": {
                "op": "image",
                "source": "alternate",
                "zoom": {
                  "sum": [
                    {
                      "curve": {
                        "from": 0,
                        "to": 0.4,
                        "scope": "scene"
                      }
                    },
                    {
                      "curve": {
                        "from": 0,
                        "to": 0.1,
                        "scope": "local"
                      }
                    }
                  ]
                },
                "flip_probability": 0.5,
                "zoom_baseline": {
                  "curve": {
                    "from": 0,
                    "to": 0.4,
                    "scope": "scene"
                  }
                },
                "animation": {
                  "every": 4
                }
              }
            },
            {
              "node": {
                "op": "word",
                "source": "primary",
                "visibility": [
                  0,
                  0.25
                ],
                "selection_probability": 0.5
              }
            }
          ],
          "cadence": {
            "ramp": {
              "from": 56,
              "to": 12,
              "steps": 140,
              "ease": "early"
            }
          },
          "duration": 2048
        }
      },
      {
        "node": {
          "op": "spiral",
          "speed": {
            "curve": {
              "from": 1,
              "to": 4,
              "scope": "scene"
            }
          }
        }
      }
    ],
    "duration": 2048,
    "id": "accelerate"
  }
})v4";
  case 2: return R"v4({
  "version": 4,
  "root": {
    "kind": "sequential",
    "children": [
      {
        "node": {
          "kind": "parallel",
          "children": [
            {
              "node": {
                "kind": "parallel",
                "children": [
                  {
                    "node": {
                      "op": "image",
                      "source": "primary",
                      "zoom": {
                        "curve": {
                          "from": 0,
                          "to": 0.5,
                          "scope": "local"
                        }
                      },
                      "animation": {
                        "every": 2
                      }
                    }
                  }
                ],
                "cadence": 64
              }
            },
            {
              "node": {
                "kind": "parallel",
                "children": [
                  {
                    "node": {
                      "op": "caption",
                      "source": "primary"
                    }
                  }
                ],
                "cadence": 64
              }
            },
            {
              "node": {
                "op": "spiral",
                "speed": 2
              }
            }
          ],
          "duration": 1024
        }
      },
      {
        "node": {
          "kind": "parallel",
          "children": [
            {
              "node": {
                "kind": "parallel",
                "children": [
                  {
                    "node": {
                      "op": "image",
                      "source": "secondary",
                      "zoom": {
                        "curve": {
                          "from": 0,
                          "to": 0.8,
                          "scope": "fast_section"
                        }
                      }
                    }
                  }
                ],
                "cadence": 8
              }
            },
            {
              "node": {
                "kind": "parallel",
                "children": [
                  {
                    "node": {
                      "op": "word",
                      "source": "secondary"
                    }
                  }
                ],
                "cadence": 16
              }
            },
            {
              "node": {
                "op": "spiral",
                "speed": 4
              }
            }
          ],
          "duration": 512,
          "id": "fast_section"
        }
      }
    ],
    "duration": 1536,
    "repeat": 2,
    "id": "slow_flash"
  }
})v4";
  case 3: return R"v4({
  "version": 4,
  "root": {
    "kind": "parallel",
    "children": [
      {
        "node": {
          "kind": "parallel",
          "children": [
            {
              "node": {
                "op": "image",
                "source": "either",
                "zoom": {
                  "curve": {
                    "from": 0,
                    "to": 0.375,
                    "scope": "local"
                  }
                },
                "animation": {
                  "every": 3
                }
              }
            }
          ],
          "cadence": 64
        }
      },
      {
        "node": {
          "kind": "parallel",
          "children": [
            {
              "node": {
                "op": "subtext",
                "source": "secondary"
              }
            }
          ],
          "cadence": 32
        }
      },
      {
        "node": {
          "op": "spiral",
          "speed": 4
        }
      }
    ],
    "duration": 1024,
    "id": "sub_text"
  }
})v4";
  case 4: return R"v4({
  "version": 4,
  "root": {
    "kind": "parallel",
    "children": [
      {
        "node": {
          "kind": "parallel",
          "children": [
            {
              "node": {
                "op": "image",
                "source": "secondary",
                "zoom": {
                  "curve": {
                    "from": 0,
                    "to": 0.8,
                    "scope": "local"
                  }
                }
              }
            }
          ],
          "cadence": 128
        }
      },
      {
        "node": {
          "kind": "parallel",
          "children": [
            {
              "node": {
                "op": "image",
                "source": "secondary",
                "zoom": {
                  "curve": {
                    "from": 0,
                    "to": 0.8,
                    "scope": "local"
                  }
                },
                "opacity": {
                  "triangle": {
                    "from": 0,
                    "peak": 1,
                    "to": 0
                  }
                }
              }
            }
          ],
          "cadence": 128,
          "offset": 64
        }
      },
      {
        "node": {
          "kind": "parallel",
          "children": [
            {
              "node": {
                "op": "word",
                "source": "secondary"
              }
            }
          ],
          "cadence": 64
        }
      },
      {
        "node": {
          "kind": "parallel",
          "children": [
            {
              "node": {
                "op": "caption",
                "source": "primary"
              }
            }
          ],
          "cadence": 32
        }
      },
      {
        "node": {
          "op": "spiral",
          "speed": 2
        }
      }
    ],
    "duration": 1024,
    "id": "flash_text"
  }
})v4";
  case 5: return R"v4({
  "root": {
    "children": [
      {
        "node": {
          "cadence": 64,
          "children": [
            {
              "node": {
                "animation": {
                  "every": 3
                },
                "op": "image",
                "source": "primary",
                "zoom": {
                  "curve": {
                    "from": 0,
                    "scope": "local",
                    "to": 0.5
                  }
                }
              }
            }
          ],
          "kind": "parallel"
        }
      },
      {
        "node": {
          "cadence": 64,
          "children": [
            {
              "node": {
                "animation": {
                  "every": 3
                },
                "op": "image",
                "opacity": 0.5,
                "source": "secondary",
                "zoom": {
                  "curve": {
                    "from": 0,
                    "scope": "local",
                    "to": 0.5
                  }
                }
              }
            }
          ],
          "kind": "parallel",
          "offset": 32
        }
      },
      {
        "node": {
          "cadence": 32,
          "children": [
            {
              "node": {
                "op": "caption",
                "source": "primary"
              }
            }
          ],
          "kind": "parallel"
        }
      },
      {
        "node": {
          "op": "spiral",
          "speed": 3
        }
      }
    ],
    "duration": 2048,
    "id": "parallel",
    "kind": "parallel"
  },
  "version": 4
})v4";
  case 6: return R"v4({
  "version": 4,
  "root": {
    "kind": "parallel",
    "children": [
      {
        "node": {
          "kind": "parallel",
          "children": [
            {
              "node": {
                "op": "image",
                "source": "primary",
                "zoom": {
                  "curve": {
                    "from": 0,
                    "to": 0.875,
                    "scope": "local"
                  }
                },
                "animation": {
                  "every": 2
                }
              }
            }
          ],
          "cadence": 96
        }
      },
      {
        "node": {
          "kind": "parallel",
          "children": [
            {
              "node": {
                "op": "image",
                "source": "primary",
                "zoom": {
                  "curve": {
                    "from": 0,
                    "to": 0.875,
                    "scope": "local"
                  }
                },
                "opacity": 0.5
              }
            }
          ],
          "cadence": 96,
          "offset": 32
        }
      },
      {
        "node": {
          "kind": "parallel",
          "children": [
            {
              "node": {
                "op": "image",
                "source": "secondary",
                "zoom": {
                  "curve": {
                    "from": 0,
                    "to": 0.875,
                    "scope": "local"
                  }
                },
                "opacity": 0.33
              }
            }
          ],
          "cadence": 96,
          "offset": 64
        }
      },
      {
        "node": {
          "kind": "parallel",
          "children": [
            {
              "node": {
                "op": "word",
                "source": "either"
              }
            }
          ],
          "cadence": 32
        }
      },
      {
        "node": {
          "op": "spiral",
          "speed": 3
        }
      }
    ],
    "duration": 1152,
    "id": "super_parallel"
  }
})v4";
  case 7: return R"v4({
  "version": 4,
  "root": {
    "kind": "parallel",
    "children": [
      {
        "node": {
          "kind": "parallel",
          "children": [
            {
              "node": {
                "op": "image",
                "source": "either",
                "zoom": {
                  "curve": {
                    "from": 0,
                    "to": 0.625,
                    "scope": "local"
                  }
                },
                "animation": {
                  "every": 1
                }
              }
            }
          ],
          "cadence": 64
        }
      },
      {
        "node": {
          "kind": "parallel",
          "children": [
            {
              "node": {
                "op": "image",
                "source": "secondary",
                "zoom": {
                  "curve": {
                    "from": 0.5,
                    "to": 0.625,
                    "scope": "local"
                  }
                },
                "envelope": {
                  "in": 8,
                  "hold": 16,
                  "out": 8
                }
              }
            }
          ],
          "cadence": 64,
          "offset": 48
        }
      },
      {
        "node": {
          "kind": "parallel",
          "children": [
            {
              "node": {
                "op": "caption",
                "source": "either"
              }
            }
          ],
          "cadence": 32
        }
      },
      {
        "node": {
          "op": "spiral",
          "speed": 3
        }
      }
    ],
    "duration": 1024,
    "id": "animation"
  }
})v4";
  case 8: return R"v4({
  "version": 4,
  "root": {
    "kind": "parallel",
    "children": [
      {
        "node": {
          "kind": "random",
          "children": [
            {
              "node": {
                "kind": "parallel",
                "children": [
                  {
                    "node": {
                      "kind": "parallel",
                      "children": [
                        {
                          "node": {
                            "op": "image",
                            "source": "either",
                            "zoom": {
                              "curve": {
                                "from": 0.0625,
                                "to": 0.1875,
                                "scope": "local"
                              }
                            }
                          }
                        }
                      ],
                      "cadence": 8
                    }
                  }
                ]
              },
              "exit": {
                "probability": {
                  "numerator": 1,
                  "denominator": 12
                },
                "check_every": 8
              }
            },
            {
              "node": {
                "kind": "parallel",
                "children": [
                  {
                    "node": {
                      "op": "image",
                      "source": "either",
                      "zoom": {
                        "curve": {
                          "from": 0.1,
                          "to": 0.7,
                          "scope": "local"
                        }
                      },
                      "animation": {
                        "every": 1
                      }
                    }
                  }
                ],
                "duration": {
                  "min": 64,
                  "max": 128,
                  "step": 8
                }
              }
            }
          ],
          "select": {
            "order": "round_robin",
            "allow_self": false,
            "first": 0,
            "cooldown": 64
          }
        }
      },
      {
        "node": {
          "kind": "parallel",
          "children": [
            {
              "node": {
                "op": "word",
                "source": "primary",
                "selection_probability": 0.25
              }
            }
          ],
          "cadence": 8
        }
      },
      {
        "node": {
          "op": "spiral",
          "speed": 3
        }
      }
    ],
    "duration": 2048,
    "id": "super_fast"
  }
})v4";
  default: return {};
  }
}
}
