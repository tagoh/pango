/* Pango
 * pango-ot-ruleset.c: Shaping using OpenType features
 *
 * Copyright (C) 2000 Red Hat Software
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <config.h>

#include "pango-ot-private.h"
#include "pango-impl-utils.h"

typedef struct _PangoOTRule PangoOTRule;

struct _PangoOTRule
{
  gulong     property_bit;
  FT_UShort  feature_index;
  guint      table_type : 1;
};

static void pango_ot_ruleset_class_init (GObjectClass   *object_class);
static void pango_ot_ruleset_init       (PangoOTRuleset *ruleset);
static void pango_ot_ruleset_finalize   (GObject        *object);

static GObjectClass *parent_class;

GType
pango_ot_ruleset_get_type (void)
{
  static GType object_type = 0;

  if (!object_type)
    {
      const GTypeInfo object_info =
      {
	sizeof (PangoOTRulesetClass),
	(GBaseInitFunc) NULL,
	(GBaseFinalizeFunc) NULL,
	(GClassInitFunc)pango_ot_ruleset_class_init,
	NULL,           /* class_finalize */
	NULL,           /* class_data */
	sizeof (PangoOTRuleset),
	0,              /* n_preallocs */
	(GInstanceInitFunc)pango_ot_ruleset_init,
	NULL            /* value_table */
      };

      object_type = g_type_register_static (G_TYPE_OBJECT,
					    I_("PangoOTRuleset"),
					    &object_info, 0);
    }

  return object_type;
}

static void
pango_ot_ruleset_class_init (GObjectClass *object_class)
{
  parent_class = g_type_class_peek_parent (object_class);

  object_class->finalize = pango_ot_ruleset_finalize;
}

static void
pango_ot_ruleset_init (PangoOTRuleset *ruleset)
{
  ruleset->rules = g_array_new (FALSE, FALSE, sizeof (PangoOTRule));
}

static void
pango_ot_ruleset_finalize (GObject *object)
{
  PangoOTRuleset *ruleset = PANGO_OT_RULESET (object);

  g_array_free (ruleset->rules, TRUE);
  if (ruleset->info)
    g_object_remove_weak_pointer (ruleset->info, &ruleset->info);

  parent_class->finalize (object);
}

/**
 * pango_ot_ruleset_new:
 * @info: a #PangoOTInfo.
 *
 * Creates a new #PangoOTRuleset for the given OpenType info.
 *
 * Return value: the newly allocated #PangoOTRuleset, which
 *               should be freed with g_object_unref().
 **/
PangoOTRuleset *
pango_ot_ruleset_new (PangoOTInfo *info)
{
  PangoOTRuleset *ruleset;

  g_return_val_if_fail (info != NULL, NULL);

  ruleset = g_object_new (PANGO_TYPE_OT_RULESET, NULL);

  ruleset->info = info;
  g_object_add_weak_pointer (ruleset->info, &ruleset->info);

  ruleset->script_index[0]   = PANGO_OT_NO_SCRIPT;
  ruleset->script_index[1]   = PANGO_OT_NO_SCRIPT;
  ruleset->language_index[0] = PANGO_OT_DEFAULT_LANGUAGE;
  ruleset->language_index[1] = PANGO_OT_DEFAULT_LANGUAGE;

  return ruleset;
}

/**
 * pango_ot_ruleset_new_for:
 * @info: a #PangoOTInfo.
 * @script: a #PangoScript.
 * @language: a #PangoLanguage.
 *
 * Creates a new #PangoOTRuleset for the given OpenType info, script, and
 * language.
 *
 * This function is part of a convenience scheme that highly simplifies
 * using a #PangoOTRuleset to represent features for a specific pair of script
 * and language.  So one can use this function passing in the script and
 * language of interest, and later try to add features to the ruleset by just
 * specifying the feature name or tag, without having to deal with finding
 * script, language, or feature indices manually.
 *
 * In excess to what pango_ot_ruleset_new() does, this function will:
 * <itemizedlist>
 *   <listitem>
 *   Find the #PangoOTTag script and language tags associated with
 *   @script and @language using pango_ot_tag_from_script() and
 *   pango_ot_tag_from_language(),
 *   </listitem>
 *   <listitem>
 *   For each of table types %PANGO_OT_TABLE_GSUB and %PANGO_OT_TABLE_GPOS,
 *   find the script index of the script tag found and the language
 *   system index of the language tag found in that script system, using
 *   pango_ot_info_find_script() and pango_ot_info_find_language(),
 *   </listitem>
 *   <listitem>
 *   For found language-systems, if they have required feature
 *   index, add that feature to the ruleset using
 *   pango_ot_ruleset_add_feature(),
 *   </listitem>
 *   <listitem>
 *   Remember found script and language indices for both table types,
 *   and use them in future pango_ot_ruleset_maybe_add_feature() and
 *   pango_ot_ruleset_maybe_add_features().
 *   </listitem>
 * </itemizedlist>
 *
 * Because of the way return values of pango_ot_info_find_script() and
 * pango_ot_info_find_language() are ignored, this function automatically
 * finds and uses the 'DFLT' script and the default language-system.
 *
 * Return value: the newly allocated #PangoOTRuleset, which
 *               should be freed with g_object_unref().
 *
 * Since: 1.18
 **/
PangoOTRuleset *
pango_ot_ruleset_new_for (PangoOTInfo       *info,
			  PangoScript        script,
			  PangoLanguage     *language)
{
  PangoOTRuleset *ruleset = pango_ot_ruleset_new (info);
  PangoOTTag script_tag, language_tag;
  PangoOTTableType table_type;

  script_tag   = pango_ot_tag_from_script (script);
  language_tag = pango_ot_tag_from_language (language);

  for (table_type = PANGO_OT_TABLE_GSUB; table_type <= PANGO_OT_TABLE_GPOS; table_type++)
    {
      guint script_index, language_index, feature_index;

      pango_ot_info_find_script   (ruleset->info, table_type,
				   script_tag, &script_index);
      pango_ot_info_find_language (ruleset->info, table_type, script_index,
				   language_tag, &language_index,
				   &feature_index);

      ruleset->script_index[table_type] = script_index;
      ruleset->language_index[table_type] = language_index;

      /* add required feature of the language */
      pango_ot_ruleset_add_feature (ruleset, table_type,
				    feature_index, PANGO_OT_ALL_GLYPHS);
    }

  return ruleset;
}

/**
 * pango_ot_ruleset_add_feature:
 * @ruleset: a #PangoOTRuleset.
 * @table_type: the table type to add a feature to.
 * @feature_index: the index of the feature to add.
 * @property_bit: the property bit to use for this feature. Used to identify
 *                the glyphs that this feature should be applied to, or
 *                %PANGO_OT_ALL_GLYPHS if it should be applied to all glyphs.
 *
 * Adds a feature to the ruleset.
 **/
void
pango_ot_ruleset_add_feature (PangoOTRuleset   *ruleset,
			      PangoOTTableType  table_type,
			      guint             feature_index,
			      gulong            property_bit)
{
  PangoOTRule tmp_rule;

  g_return_if_fail (PANGO_IS_OT_RULESET (ruleset));
  g_return_if_fail (PANGO_IS_OT_INFO (ruleset->info));

  if (feature_index == PANGO_OT_NO_FEATURE)
    return;

  tmp_rule.table_type = table_type;
  tmp_rule.feature_index = feature_index;
  tmp_rule.property_bit = property_bit;

  g_array_append_val (ruleset->rules, tmp_rule);
}

/**
 * pango_ot_ruleset_maybe_add_feature:
 * @ruleset: a #PangoOTRuleset.
 * @table_type: the table type to add a feature to.
 * @feature_tag: the tag of the feature to add.
 * @property_bit: the property bit to use for this feature. Used to identify
 *                the glyphs that this feature should be applied to, or
 *                %PANGO_OT_ALL_GLYPHS if it should be applied to all glyphs.
 *
 * This is a convenience function that first tries to find the feature
 * using pango_ot_info_find_feature() and the ruleset script and language
 * passed to pango_ot_ruleset_new_for(),
 * and if the feature is found, adds it to the ruleset.
 *
 * If @ruleset was not created using pango_ot_ruleset_new_for(), this function
 * does nothing.
 *
 * Return value: %TRUE if the feature was found and added to ruleset,
 *               %FALSE otherwise.
 *
 * Since: 1.18
 **/
gboolean
pango_ot_ruleset_maybe_add_feature (PangoOTRuleset          *ruleset,
				    PangoOTTableType         table_type,
				    PangoOTTag               feature_tag,
				    gulong                   property_bit)
{
  guint feature_index;

  g_return_val_if_fail (PANGO_IS_OT_RULESET (ruleset), FALSE);
  g_return_val_if_fail (PANGO_IS_OT_INFO (ruleset->info), FALSE);

  pango_ot_info_find_feature (ruleset->info, table_type,
			      feature_tag,
			      ruleset->script_index[table_type],
			      ruleset->language_index[table_type],
			      &feature_index);

  if (feature_index != PANGO_OT_NO_FEATURE)
    {
      pango_ot_ruleset_add_feature (ruleset, table_type,
				    feature_index, property_bit);
      return TRUE;
    }

  return FALSE;
}

/**
 * pango_ot_ruleset_maybe_add_features:
 * @ruleset: a #PangoOTRuleset.
 * @table_type: the table type to add features to.
 * @features: array of feature name and property bits to add.
 * @n_features: number of feature records in @features array.
 *
 * This is a convenience function that 
 * for each feature in the feature map array @features
 * converts the feature name to a #PangoOTTag feature tag using FT_MAKE_TAG()
 * and calls pango_ot_ruleset_maybe_add_feature() on it.
 *
 * Return value: The number of features in @features that were found
 *               and added to @ruleset.
 *
 * Since: 1.18
 **/
int
pango_ot_ruleset_maybe_add_features (PangoOTRuleset          *ruleset,
				     PangoOTTableType         table_type,
				     const PangoOTFeatureMap *features,
				     int                      n_features)
{
  int i, n_found_features = 0;

  g_return_val_if_fail (PANGO_IS_OT_RULESET (ruleset), 0);
  g_return_val_if_fail (PANGO_IS_OT_INFO (ruleset->info), 0);

  for (i = 0; i < n_features; i++)
    {
      PangoOTTag feature_tag = FT_MAKE_TAG (features[i].feature_name[0],
					    features[i].feature_name[1],
					    features[i].feature_name[2],
					    features[i].feature_name[3]);

      n_found_features += pango_ot_ruleset_maybe_add_feature (ruleset,
							      table_type,
							      feature_tag,
							      features[i].property_bit);
    }

  return n_found_features;
}

/**
 * pango_ot_ruleset_substitute:
 * @ruleset: a #PangoOTRuleset.
 * @buffer: a #PangoOTBuffer.
 *
 * Performs the OpenType GSUB substitution on @buffer using the features
 * in @ruleset
 *
 * Since: 1.4
 **/
void
pango_ot_ruleset_substitute  (const PangoOTRuleset *ruleset,
			      PangoOTBuffer        *buffer)
{
  unsigned int i;

  HB_GSUB gsub = NULL;

  g_return_if_fail (PANGO_IS_OT_RULESET (ruleset));
  g_return_if_fail (PANGO_IS_OT_INFO (ruleset->info));

  for (i = 0; i < ruleset->rules->len; i++)
    {
      PangoOTRule *rule = &g_array_index (ruleset->rules, PangoOTRule, i);

      if (rule->table_type != PANGO_OT_TABLE_GSUB)
	continue;

      if (!gsub)
	{
	  gsub = pango_ot_info_get_gsub (ruleset->info);

	  if (gsub)
	    HB_GSUB_Clear_Features (gsub);
	  else
	    return;
	}

      HB_GSUB_Add_Feature (gsub, rule->feature_index, rule->property_bit);
    }

  HB_GSUB_Apply_String (gsub, buffer->buffer);
}

/**
 * pango_ot_ruleset_position:
 * @ruleset: a #PangoOTRuleset.
 * @buffer: a #PangoOTBuffer.
 *
 * Performs the OpenType GPOS positioning on @buffer using the features
 * in @ruleset
 *
 * Since: 1.4
 **/
void
pango_ot_ruleset_position (const PangoOTRuleset *ruleset,
			   PangoOTBuffer        *buffer)
{
  unsigned int i;

  HB_GPOS gpos = NULL;

  g_return_if_fail (PANGO_IS_OT_RULESET (ruleset));
  g_return_if_fail (PANGO_IS_OT_INFO (ruleset->info));

  for (i = 0; i < ruleset->rules->len; i++)
    {
      PangoOTRule *rule = &g_array_index (ruleset->rules, PangoOTRule, i);

      if (rule->table_type != PANGO_OT_TABLE_GPOS)
	continue;

      if (!gpos)
	{
	  gpos = pango_ot_info_get_gpos (ruleset->info);

	  if (gpos)
	    HB_GPOS_Clear_Features (gpos);
	  else
	    return;
	}

      HB_GPOS_Add_Feature (gpos, rule->feature_index, rule->property_bit);
    }

  if (HB_GPOS_Apply_String (ruleset->info->face, gpos, 0, buffer->buffer,
			    FALSE /* enable device-dependant values */,
			    buffer->rtl) == FT_Err_Ok)
    buffer->applied_gpos = TRUE;
}

